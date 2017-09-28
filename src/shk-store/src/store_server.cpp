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

#include <rs/empty.h>
#include <rs/map.h>
#include <rs/pipe.h>
#include <util/string_view.h>

#include "constants.h"

namespace shk {
namespace {

class StoreServer : public Store {
 public:
  StoreServer(const std::shared_ptr<google::bigtable::v2::Bigtable> &bigtable)
      : bigtable_(bigtable) {}

  AnyPublisher<StoreInsertResponse> Insert(
      const CallContext &ctx, StoreInsertRequest &&request) override {
    google::bigtable::v2::MutateRowRequest write;
    write.set_table_name(kShkStoreTableName);
    write.set_row_key(HashContents(request.contents()));

    auto &mutation = *write.add_mutations();
    auto &set_cell = *mutation.mutable_set_cell();
    set_cell.set_family_name(kShkStoreContentsFamily);
    set_cell.set_column_qualifier(kShkStoreContentsColumn);
    set_cell.set_timestamp_micros(-1);  // TODO(peck): Handle expiry time
    set_cell.set_value(std::move(*request.mutable_contents()));

    return AnyPublisher<StoreInsertResponse>(Pipe(
        bigtable_->MutateRow(ctx, std::move(write)),
        Map([](google::bigtable::v2::MutateRowResponse response) {
          // TODO(peck): Handle this
          return StoreInsertResponse();
        })));
  }

  AnyPublisher<StoreTouchResponse> Touch(
      const CallContext &ctx, StoreTouchRequest &&request) override {
    return AnyPublisher<StoreTouchResponse>(Empty());
  }

  AnyPublisher<StoreGetResponse> Get(
      const CallContext &ctx, StoreGetRequest &&request) override {
    google::bigtable::v2::ReadRowsRequest read;
    read.set_table_name(kShkStoreTableName);
    read.mutable_rows()->add_row_keys(request.key());

    return AnyPublisher<StoreGetResponse>(Pipe(
        bigtable_->ReadRows(ctx, std::move(read)),
        Map([](google::bigtable::v2::ReadRowsResponse response) {
          // TODO(peck): Handle this
          return StoreGetResponse();
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
