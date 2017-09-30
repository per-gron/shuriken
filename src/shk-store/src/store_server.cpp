// Copyright 2017 Per Grön. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "store_server.h"

#include <blake2.h>
#include <rs/concat_map.h>
#include <rs/empty.h>
#include <rs/from.h>
#include <rs/if_empty.h>
#include <rs/map.h>
#include <rs/pipe.h>
#include <rs/reduce.h>
#include <rs/throw.h>
#include <util/hash.h>
#include <util/string_view.h>

#include "constants.h"

namespace shk {
namespace {

/**
 * Wraps (and owns) a protobuf object and exposes one of its repeated fields as
 * an STL-style container, for use with the From rs operator.
 */
template <
    typename Message,
    typename ElementType,
    int (Message::*GetSize)() const,
    ElementType *(Message::*GetElement)(int index)>
class ProtobufContainer {
 public:
  template <typename IteratorMessage, typename IteratorElementType>
  class Iterator {
   public:
    Iterator(IteratorMessage *message, int index)
        : message_(message), index_(index) {}

    IteratorElementType &operator*() {
      return *(message_->*GetElement)(index_);
    }

    const IteratorElementType &operator*() const {
      return *(message_->*GetElement)(index_);
    }

    Iterator &operator++() {
      index_++;
      return *this;
    }

    bool operator==(const Iterator &other) {
      return message_ == other.message_ && index_ == other.index_;
    }

   private:
    IteratorMessage *message_;
    int index_;
  };

  ProtobufContainer(Message &&message)
      : message_(std::move(message)) {}

  Iterator<Message, ElementType> begin() {
    return Iterator<Message, ElementType>(&message_, 0);
  }

  Iterator<const Message, const ElementType> begin() const {
    return Iterator<const Message, const ElementType>(&message_, 0);
  }

  Iterator<Message, ElementType> end() {
    return Iterator<Message, ElementType>(&message_, (message_.*GetSize)());
  }

  Iterator<const Message, const ElementType> end() const {
    return Iterator<const Message, const ElementType>(
        &message_, (message_.*GetSize())());
  }

 private:
  Message message_;
};

/**
 * This is an rs operator that is a little bit like Reduce, but it is a little
 * bit more flexible: For each incoming value, it allows emitting the
 * accumulator value instead of only emitting a value at the end. If there
 * are no input values, the output stream is also empty. If there are values,
 * the accumulator is always emitted after the input stream ends.
 *
 * The signature of the Reducer function is:
 *
 * void Reducer(Accumulator &&accum, Value &&value, bool *emit_now);
 *
 * If *emit_now is set to true, the returned Accumulator value is emitted
 * immediately and the next call to Reducer will get a default constructed
 * Accumulator value.
 */
template <typename Accumulator, typename Reducer>
auto ReduceMultiple(Accumulator &&initial, Reducer &&reducer) {
  // Return an operator (it takes a Publisher and returns a Publisher)
  return [
      initial = std::forward<Accumulator>(initial),
      reducer = std::forward<Reducer>(reducer)](auto source) {
    return Pipe(
        source,
        // TODO(peck): Emit the last value too
        ConcatMap([accum = initial, reducer](auto &&value) mutable {
          bool emit_now = false;
          accum = reducer(
              std::move(accum),
              std::forward<decltype(value)>(value),
              &emit_now);

          if (emit_now) {
            auto result = AnyPublisher<decltype(accum)>(
                Just(std::move(accum)));
            accum = decltype(accum)();
            return result;
          } else {
            return AnyPublisher<decltype(accum)>(Empty());
          }
        }));
  };
}

std::string HashContents(const string_view &contents) {
  // TODO(peck): Implement me
  return "";
}

auto ValidateInsertRequests() {
  return [](auto input) {
    return Pipe(
        input,
        IfEmpty(Throw(GrpcError(::grpc::Status(
            ::grpc::INVALID_ARGUMENT,
            "Got RPC with no request messages")))),
        Map([](StoreInsertRequest &&request) {
          if (request.contents().size() > kShkStoreInsertChunkSizeLimit) {
            throw GrpcError(::grpc::Status(
                ::grpc::INVALID_ARGUMENT,
                "Got too large StoreInsertRequest"));
          }
          if (request.size() < 0) {
            throw GrpcError(::grpc::Status(
                ::grpc::INVALID_ARGUMENT,
                "Got negative size StoreInsertRequest"));
          }
          if (request.expiry_time_micros() < 0) {
            throw GrpcError(::grpc::Status(
                ::grpc::INVALID_ARGUMENT,
                "Got negative expiry timestamp in StoreInsertRequest"));
          }
          return request;
        }));
  };
}

auto GroupInsertRequests() {
  // Group into cells into sufficiently large chunks to be stored in db
  return ReduceMultiple(
      StoreInsertRequest(),
      [](
          StoreInsertRequest &&accumulator,
          StoreInsertRequest &&value,
          bool *emit_now) {
        accumulator.mutable_contents()->append(value.contents());
        if (accumulator.contents().size() >=
            kShkStoreCellSplitThreshold) {
          *emit_now = true;
        }
        return accumulator;
      });
}

auto SetExpiryTimeOnAllInsertRequests() {
  // Set expiry time on all parts, not just the first one
  return Map([expiry = ::google::protobuf::int64(0)](
      StoreInsertRequest &&request) mutable {
    if (expiry == 0) {
      expiry = request.expiry_time_micros();
    } else {
      request.set_expiry_time_micros(expiry);
    }
    return request;
  });
}

/**
 * This is an operator that takes a stream of chunked insert requests and
 * ensures that they can be written to the db: Set the key to be the key for
 * each write and add a multi entry if needed.
 *
 * This might return entries (after the initial one) that don't have
 * expiry_time_micros set; that needs to be populated. It might also return
 * entries with an empty key; they should not be written.
 */
auto AddMultiEntryInsertIfChunked() {
  return [](auto input) {
    // A special fake insert value added to the end that may or may not be
    // replaced with a multi entry write. Size -1 ensures that it is
    // distinguishable from other inserts because any such insert would have
    // already caused the RPC to fail.
    StoreInsertRequest sentinel_insert;
    sentinel_insert.set_size(-1);

    return Pipe(
        Concat(input, Just(sentinel_insert)),
        Map([
            num_inserts = 0,
            claimed_key = std::string(),
            hash_state = blake2b_state()](
                StoreInsertRequest &&request) mutable {
          if (claimed_key.empty()) {
            // This must be the first StoreInsertRequest in the stream
            if (claimed_key.empty()) {
              throw GrpcError(::grpc::Status(
                  ::grpc::INVALID_ARGUMENT,
                  "key field not set on the first StoreInsertRequest"));
            }
            claimed_key = request.key();
            blake2b_init(&hash_state, kHashSize);
          }

          if (request.size() != -1) {
            num_inserts++;
            request.set_key(HashContents(request.contents()));
            blake2b_update(
                &hash_state,
                reinterpret_cast<const uint8_t *>(request.contents().data()),
                request.contents().size());
          } else {
            // This is the final sentinel value
            std::string actual_key(kHashSize, '\0');
            blake2b_final(
                &hash_state,
                static_cast<void *>(&actual_key[0]),
                actual_key.size());
            if (claimed_key != actual_key) {
              throw GrpcError(::grpc::Status(
                  ::grpc::INVALID_ARGUMENT,
                  "Got key that does not match contents"));
            }

            if (num_inserts > 1) {
              // Need to make a multi-entry
              request.set_key(actual_key);
              // TODO(peck): Need to let the db writer know that this is a multi entry
              request.set_contents("TODO");
            }
          }
          return request;
        }));
  };
}

auto WriteInsertsToDb(
    const CallContext &ctx,
    const std::shared_ptr<google::bigtable::v2::Bigtable> &bigtable) {
  return ConcatMap([ctx, bigtable](StoreInsertRequest &&request) {
    auto key = HashContents(request.contents());

    google::bigtable::v2::MutateRowRequest write;
    write.set_table_name(kShkStoreTableName);
    write.set_row_key(key);

    auto &mutation = *write.add_mutations();
    auto &set_cell = *mutation.mutable_set_cell();
    set_cell.set_family_name(kShkStoreContentsFamily);
    set_cell.set_column_qualifier(kShkStoreContentsColumn);
    set_cell.set_timestamp_micros(
        request.expiry_time_micros() - kShkStoreTableTtlMicros);
    set_cell.set_value(std::move(*request.mutable_contents()));

    return bigtable->MutateRow(ctx, std::move(write));
  });
}

auto SwallowInputAndReturnInsertResponse() {
  // TODO(peck): This Reduce call is really just to add a value to the
  // end. Change to something more intuitive.
  return Reduce(
      StoreInsertResponse(),
      [](auto &&accum, auto &&) { return accum; });
}

class StoreServer : public Store {
 public:
  StoreServer(const std::shared_ptr<google::bigtable::v2::Bigtable> &bigtable)
      : bigtable_(bigtable) {}

  AnyPublisher<StoreInsertResponse> Insert(
      const CallContext &ctx,
      AnyPublisher<StoreInsertRequest> &&requests) override {
    return AnyPublisher<StoreInsertResponse>(Pipe(
        requests,
        ValidateInsertRequests(),
        GroupInsertRequests(),
        AddMultiEntryInsertIfChunked(),
        SetExpiryTimeOnAllInsertRequests(),
        WriteInsertsToDb(ctx, bigtable_),
        SwallowInputAndReturnInsertResponse()));
  }

  AnyPublisher<StoreTouchResponse> Touch(
      const CallContext &ctx, StoreTouchRequest &&request) override {
    // TODO(peck): Implement me
    return AnyPublisher<StoreTouchResponse>(Empty());
  }

  AnyPublisher<StoreGetResponse> Get(
      const CallContext &ctx, StoreGetRequest &&request) override {
    // TODO(peck): Handle split entries

    google::bigtable::v2::ReadRowsRequest read;
    read.set_table_name(kShkStoreTableName);
    read.mutable_rows()->add_row_keys(request.key());

    return AnyPublisher<StoreGetResponse>(Pipe(
        bigtable_->ReadRows(ctx, std::move(read)),
        ConcatMap([](google::bigtable::v2::ReadRowsResponse &&response) {
          return From(ProtobufContainer<
              google::bigtable::v2::ReadRowsResponse,
              google::bigtable::v2::ReadRowsResponse::CellChunk,
              &google::bigtable::v2::ReadRowsResponse::chunks_size,
              &google::bigtable::v2::ReadRowsResponse::mutable_chunks>(
                  std::move(response)));
        }),
        Map([](google::bigtable::v2::ReadRowsResponse::CellChunk &&chunk) {
          StoreGetResponse response;
          if (chunk.timestamp_micros() != 0) {
            response.set_expiry_time_micros(
                chunk.timestamp_micros() + kShkStoreTableTtlMicros);
            response.set_size(chunk.value_size());
          }
          response.set_contents(std::move(*chunk.mutable_value()));
          response.set_reset_row(chunk.reset_row());
          return response;
        })));
  }

 private:
  std::shared_ptr<google::bigtable::v2::Bigtable> bigtable_;
};

}  // anonymous namespace

std::unique_ptr<Store> MakeStore(
    const std::shared_ptr<google::bigtable::v2::Bigtable> &bigtable) {
  return std::unique_ptr<Store>(new StoreServer(bigtable));
}

}  // namespace shk
