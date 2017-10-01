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
#include "protobuf_container.h"
#include "reduce_multiple.h"

namespace shk {
namespace {

template <typename Publisher>
auto Append(Publisher &&appended_publisher) {
  return [appended_publisher = std::forward<Publisher>(appended_publisher)](
      auto &&stream) {
    return Concat(
        std::forward<decltype(stream)>(stream),
        appended_publisher);
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

enum class InsertValidations {
  ALL,
  BYPASS_CHUNK_LIMIT
};

auto ValidateInsertRequests(InsertValidations insert_validations) {
  return [insert_validations](auto input) {
    return Pipe(
        input,
        IfEmpty(Throw(GrpcError(::grpc::Status(
            ::grpc::INVALID_ARGUMENT,
            "Got RPC with no request messages")))),
        Map([insert_validations](StoreInsertRequest &&request) {
          if (insert_validations != InsertValidations::BYPASS_CHUNK_LIMIT &&
              request.contents().size() > kShkStoreInsertChunkSizeLimit) {
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
      [](StoreInsertRequest &&accum, StoreInsertRequest &&value) {
        accum.mutable_contents()->append(value.contents());
        return accum;
      },
      [](const StoreInsertRequest &accum, const StoreInsertRequest &value) {
        return accum.contents().size() >= kShkStoreCellSplitThreshold;
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
    set_cell.set_column_qualifier(entry_to_write.multi_entry() ?
        kShkStoreMultiEntryColumn : kShkStoreDataColumn);
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

/**
 * Takes a stream of ReadRowsResponse messages and emits a stream of all the
 * CellChunk messages they contain.
 */
auto FlattenReadRowsResponsesToCellChunks() {
  return ConcatMap([](google::bigtable::v2::ReadRowsResponse &&response) {
    return From(ProtobufContainer<
        google::bigtable::v2::ReadRowsResponse,
        google::bigtable::v2::ReadRowsResponse::CellChunk,
        &google::bigtable::v2::ReadRowsResponse::chunks_size,
        &google::bigtable::v2::ReadRowsResponse::mutable_chunks>(
            std::move(response)));
  });
}

auto MultiEntryEntries(MultiEntry &&multi_entry) {
  return From(ProtobufContainer<
      MultiEntry,
      MultiEntry::Entry,
      &MultiEntry::entry_size,
      &MultiEntry::mutable_entry>(std::move(multi_entry)));
}

/**
 * Class that takes a stream of CellChunks from Bigtable and parses them into a
 * single continuous value.
 */
class CellChunkReader {
 public:
  void ReadChunk(
      const google::bigtable::v2::ReadRowsResponse::CellChunk &chunk) {
    if (chunk.reset_row()) {
      buffer_.clear();
    }
    if (chunk.value_size() > 0) {
      buffer_.reserve(chunk.value_size());
    }
    buffer_.append(chunk.value());
  }

  /**
   * This method steals the internal buffer of this class. It can only be called
   * once.
   */
  std::string ExtractData() {
    return std::move(buffer_);
  }

  ::google::protobuf::int64 ExpiryTimeMicros() {
    return timestamp_micros_;
  }

 private:
  ::google::protobuf::int64 timestamp_micros_ = 0;
  std::string buffer_;
};

StoreGetResponse CellChunkToStoreGetResponse(
    google::bigtable::v2::ReadRowsResponse::CellChunk &&chunk,
    bool reset_checkpoint) {
  StoreGetResponse response;
  if (chunk.timestamp_micros() != 0) {
    response.set_expiry_time_micros(
        chunk.timestamp_micros() + kShkStoreTableTtlMicros);
    response.set_size(chunk.value_size());
  }
  response.set_contents(std::move(*chunk.mutable_value()));
  response.set_reset_row(chunk.reset_row());
  response.set_reset_checkpoint(reset_checkpoint);
  return response;
}

class StoreServer : public Store {
 public:
  StoreServer(const std::shared_ptr<google::bigtable::v2::Bigtable> &bigtable)
      : bigtable_(bigtable) {}

  AnyPublisher<StoreInsertResponse> Insert(
      const CallContext &ctx,
      AnyPublisher<StoreInsertRequest> &&requests) override {
    return Insert(ctx, std::move(requests), InsertValidations::ALL);
  }

  AnyPublisher<StoreTouchResponse> Touch(
      const CallContext &ctx, StoreTouchRequest &&request) override {
    // TODO(peck): Make it less expensive when there is already a sufficiently
    // up-to-date entry.

    return AnyPublisher<StoreTouchResponse>(Pipe(
        Get(ctx, request.key()),
        // It is necessary to handle reset_row. In order to do that, we exploit
        // the fact that we know that reset_checkpoints occur as often as
        // entries are chunked: It is safe to buffer a whole checkpoint in
        // memory (it is small enough).
        ReduceMultiple(
            StoreGetResponse(),
            [](StoreGetResponse &&accum, StoreGetResponse &&value) {
              if (value.reset_row()) {
                accum.mutable_contents()->clear();
              }
              accum.mutable_contents()->append(value.contents());
              return accum;
            },
            [](const StoreGetResponse &accum, const StoreGetResponse &value) {
              return value.reset_checkpoint();
            }),
        Map([request, first = true](StoreGetResponse &&response) mutable {
          StoreInsertRequest insert_request;
          if (first) {
            insert_request.set_key(request.key());
            insert_request.set_size(response.size());
            insert_request.set_expiry_time_micros(request.expiry_time_micros());
            first = false;
          }
          insert_request.set_contents(std::move(*response.mutable_contents()));
          return insert_request;
        }),

        [this, ctx](auto &&insert_request_publisher) {
          // Need to bypass the chunk limit, since we have already grouped the
          // writes to db entry size.
          return Insert(
              ctx,
              AnyPublisher<StoreInsertRequest>(insert_request_publisher),
              InsertValidations::BYPASS_CHUNK_LIMIT);
        },
        Map([](StoreInsertResponse &&response) {
          return StoreTouchResponse();
        })));
  }

  AnyPublisher<StoreGetResponse> Get(
      const CallContext &ctx, StoreGetRequest &&request) override {
    return Get(ctx, request.key());
  }

 private:
  AnyPublisher<StoreInsertResponse> Insert(
      const CallContext &ctx,
      AnyPublisher<StoreInsertRequest> &&requests,
      InsertValidations insert_validations) {
    return AnyPublisher<StoreInsertResponse>(Pipe(
        requests,
        ValidateInsertRequests(insert_validations),
        GroupInsertRequests(),
        ConvertToEntriesToWrite(),
        WriteInsertsToDb(ctx, bigtable_),
        SwallowInputAndReturnInsertResponse()));
  }

  AnyPublisher<StoreGetResponse> Get(
      const CallContext &ctx, const std::string &key) {
    // TODO(peck): Make sure nonexisting entries are handled properly

    google::bigtable::v2::ReadRowsRequest read;
    read.set_table_name(kShkStoreTableName);
    read.mutable_rows()->add_row_keys(key);

    // For multi entries, we need to read the entire contents of the cell to
    // be able to parse the MultiEntry protobuf message. chunk_reader does that.
    auto chunk_reader = std::make_shared<CellChunkReader>();

    return AnyPublisher<StoreGetResponse>(Pipe(
        bigtable_->ReadRows(ctx, std::move(read)),
        FlattenReadRowsResponsesToCellChunks(),
        ConcatMap([
            chunk_reader,
            multi_entry = false,
            data_entry = false](
                google::bigtable::v2::ReadRowsResponse::CellChunk &&
                chunk) mutable {
          bool reset_checkpoint = false;
          if (!multi_entry && !data_entry) {
            // This is the first chunk. Decide if this is a multi entry or not.
            multi_entry =
                chunk.qualifier().value() == kShkStoreMultiEntryColumn;
            data_entry = !multi_entry;
            // The first chunk of a Bigtable read operation is a reset
            // checkpoint; if Bigtable sets reset_row, this is where it should
            // be reset to.
            reset_checkpoint = true;
          }

          if (multi_entry) {
            chunk_reader->ReadChunk(chunk);
            // Just return Empty here, the actual result is handled at the end.
            return AnyPublisher<StoreGetResponse>(Empty());
          } else {
            return AnyPublisher<StoreGetResponse>(Just(
                CellChunkToStoreGetResponse(
                    std::move(chunk),
                    reset_checkpoint)));
          }
        }),
        Append(MakePublisher([this, ctx, chunk_reader](auto &&subscriber) {
          std::string multi_entry_data = chunk_reader->ExtractData();
          if (multi_entry_data.empty()) {
            // This is not a multi entry. Do nothing.
            return AnySubscription(Empty().Subscribe(
                std::forward<decltype(subscriber)>(subscriber)));
          } else {
            // This *is* a multi entry. Handle it.
            auto result_stream = HandleMultiEntry(
                ctx,
                std::move(multi_entry_data),
                chunk_reader->ExpiryTimeMicros());
            return AnySubscription(result_stream.Subscribe(
                std::forward<decltype(subscriber)>(subscriber)));
          }
        }))));
  }

  AnyPublisher<StoreGetResponse> HandleMultiEntry(
      const CallContext &ctx,
      std::string &&multi_entry_data,
      ::google::protobuf::int64 expiry_time_micros) {
    MultiEntry multi_entry;
    if (!multi_entry.ParseFromString(multi_entry_data)) {
      return AnyPublisher<StoreGetResponse>(Throw(GrpcError(::grpc::Status(
          ::grpc::DATA_LOSS,
          "Encountered corrupt MultiEntry"))));
    }

    // TODO(peck): Make sure missing chunks are handled properly

    ::google::protobuf::int64 size = multi_entry.size();

    return AnyPublisher<StoreGetResponse>(Pipe(
        MultiEntryEntries(std::move(multi_entry)),
        ConcatMap([this, ctx](MultiEntry::Entry &&entry) {
          return Get(ctx, entry.key());
        }),
        Map([size, expiry_time_micros, first = true](
            StoreGetResponse &&response) mutable {
          if (response.expiry_time_micros() < expiry_time_micros) {
            // TODO(peck): Log that this is broken. Failing is overkill I think.
          }

          if (first) {
            first = false;
            response.set_size(size);
            response.set_expiry_time_micros(expiry_time_micros);
          } else {
            response.clear_size();
            response.clear_expiry_time_micros();
          }
          return response;
        })));
  }

  std::shared_ptr<google::bigtable::v2::Bigtable> bigtable_;
};

}  // anonymous namespace

std::unique_ptr<Store> MakeStore(
    const std::shared_ptr<google::bigtable::v2::Bigtable> &bigtable) {
  return std::unique_ptr<Store>(new StoreServer(bigtable));
}

}  // namespace shk
