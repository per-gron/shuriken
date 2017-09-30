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
#include <shk-store/src/internal.pb.h>
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
  blake2b_state hash_state;
  blake2b_init(&hash_state, kHashSize);
  blake2b_update(
      &hash_state,
      reinterpret_cast<const uint8_t *>(contents.data()),
      contents.size());
  std::string hash(kHashSize, '\0');
  blake2b_final(&hash_state, static_cast<void *>(&hash[0]), hash.size());
  return hash;
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

/**
 * Group into cells into sufficiently large chunks to be stored in db
 */
auto GroupInsertRequests() {
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

/**
 * This is an operator that takes a stream of chunked insert requests and
 * converts it to a stream of EntryToWrite messages that are ready to be written
 * to the database. It is responsible for potentially creating a multientry if
 * the contents are chunked, and it will set the other EntryToWrite fields, for
 * example the expiry time and the key for each write.
 */
auto ConvertToEntriesToWrite() {
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
            expiry = ::google::protobuf::int64(0),
            total_size = ::google::protobuf::int64(0),
            claimed_size = ::google::protobuf::int64(0),
            claimed_key = std::string(),
            hash_state = blake2b_state(),
            multi_entry = MultiEntry()](
                StoreInsertRequest &&request) mutable {
          if (claimed_key.empty()) {
            // This must be the first StoreInsertRequest in the stream
            if (claimed_key.empty()) {
              throw GrpcError(::grpc::Status(
                  ::grpc::INVALID_ARGUMENT,
                  "key field not set on the first StoreInsertRequest"));
            }
            expiry = request.expiry_time_micros();
            claimed_key = request.key();
            claimed_size = request.size();
            blake2b_init(&hash_state, kHashSize);
          }

          EntryToWrite entry_to_write;
          entry_to_write.set_expiry_time_micros(expiry);

          if (request.size() != -1) {
            num_inserts++;
            total_size += request.size();

            auto key = HashContents(request.contents());

            blake2b_update(
                &hash_state,
                reinterpret_cast<const uint8_t *>(request.contents().data()),
                request.contents().size());

            MultiEntry::Entry &entry = *multi_entry.add_entry();
            entry.set_start(multi_entry.size());
            entry.set_key(key);
            multi_entry.set_size(
                multi_entry.size() + request.contents().size());

            entry_to_write.set_key(key);
            entry_to_write.set_contents(std::move(*request.mutable_contents()));
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
            if (claimed_size != total_size) {
              throw GrpcError(::grpc::Status(
                  ::grpc::INVALID_ARGUMENT,
                  "Claimed size does not match actual size"));
            }

            if (num_inserts > 1) {
              // Need to make a multi-entry
              if (!multi_entry.SerializeToString(
                      entry_to_write.mutable_contents())) {
                throw GrpcError(::grpc::Status(
                    ::grpc::INTERNAL,
                    "Failed to serialize MultiEntry protobuf message"));
              }
              entry_to_write.set_key(actual_key);
              entry_to_write.set_multi_entry(true);
            }
          }
          return entry_to_write;
        }));
  };
}

auto WriteInsertsToDb(
    const CallContext &ctx,
    const std::shared_ptr<google::bigtable::v2::Bigtable> &bigtable) {
  return ConcatMap([ctx, bigtable](EntryToWrite &&entry_to_write) {
    if (entry_to_write.key().empty()) {
      return AnyPublisher<google::bigtable::v2::MutateRowResponse>(Empty());
    }

    google::bigtable::v2::MutateRowRequest write;
    write.set_table_name(kShkStoreTableName);
    write.set_row_key(entry_to_write.key());

    auto &mutation = *write.add_mutations();
    auto &set_cell = *mutation.mutable_set_cell();
    set_cell.set_family_name(kShkStoreContentsFamily);
    set_cell.set_column_qualifier(kShkStoreContentsColumn);
    set_cell.set_timestamp_micros(
        entry_to_write.expiry_time_micros() - kShkStoreTableTtlMicros);
    set_cell.set_value(std::move(*entry_to_write.mutable_contents()));

    return AnyPublisher<google::bigtable::v2::MutateRowResponse>(
        bigtable->MutateRow(ctx, std::move(write)));
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
        ConvertToEntriesToWrite(),
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
