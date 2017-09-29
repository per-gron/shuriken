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

#include <rs/concat_map.h>
#include <rs/empty.h>
#include <rs/from.h>
#include <rs/map.h>
#include <rs/pipe.h>
#include <rs/reduce.h>
#include <util/string_view.h>

#include "constants.h"

namespace shk {
namespace {

// Wraps (and owns) a protobuf object and exposes one of its repeated fields as
// an STL-style container.
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

class StoreServer : public Store {
 public:
  StoreServer(const std::shared_ptr<google::bigtable::v2::Bigtable> &bigtable)
      : bigtable_(bigtable) {}

  AnyPublisher<StoreInsertResponse> Insert(
      const CallContext &ctx,
      AnyPublisher<StoreInsertRequest> &&requests) override {
    return AnyPublisher<StoreInsertResponse>(Pipe(
        requests,
        // First, group into >=128KB cells
        ConcatMap([](StoreInsertRequest &&request) {
          if (request.contents().size() > kShkStoreInsertChunkSizeLimit) {
            return AnyPublisher<StoreInsertRequest>(
                Throw(GrpcError(::grpc::Status(
                    ::grpc::INVALID_ARGUMENT,
                    "Got too large StoreInsertRequest"))));
          }

          return AnyPublisher<StoreInsertRequest>(Empty());
        }),
        // TODO(peck): Make sure to validate the hash before writing the final
        // entry.

        // Then, store the cells
        //
        // TODO(peck): Should store the cells in parallel, which ConcatMap does
        // not do.

        // Finally, respond with one response
        //
        // TODO(peck): This Reduce call is really just to add a value to the
        // end. Change to something more intuitive.
        Reduce(
            StoreInsertResponse(),
            [](auto &&accum, auto &&) { return accum; })));

#if 0
    // auto key = HashContents(request.contents());

    google::bigtable::v2::MutateRowRequest write;
    write.set_table_name(kShkStoreTableName);
    write.set_row_key(key);

    auto &mutation = *write.add_mutations();
    auto &set_cell = *mutation.mutable_set_cell();
    set_cell.set_family_name(kShkStoreContentsFamily);
    set_cell.set_column_qualifier(kShkStoreContentsColumn);
    set_cell.set_timestamp_micros(-1);  // TODO(peck): Handle expiry time
    set_cell.set_value(std::move(*request.mutable_contents()));

    return AnyPublisher<StoreInsertResponse>(Pipe(
        bigtable_->MutateRow(ctx, std::move(write)),
        Map([](google::bigtable::v2::MutateRowResponse) {
          return StoreInsertResponse();
        })));
#endif
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
          return response;
        })));
  }

 private:
  std::string HashContents(const string_view &contents) {
    return "";
  }

  std::shared_ptr<google::bigtable::v2::Bigtable> bigtable_;
};

}  // anonymous namespace

std::unique_ptr<Store> MakeStore(
    const std::shared_ptr<google::bigtable::v2::Bigtable> &bigtable) {
  return std::unique_ptr<Store>(new StoreServer(bigtable));
}

}  // namespace shk
