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

#include <rs/catch.h>

namespace shk {
namespace detail {

CatchData::CatchData() {}

CatchData::~CatchData() {}

std::shared_ptr<CatchData> CatchData::Build() {
  return std::make_shared<CatchData>();
}

CatchSubscription::CatchSubscription(const std::shared_ptr<CatchData> &data)
    : data_(data) {}

CatchSubscription::~CatchSubscription() {}

std::shared_ptr<CatchSubscription> CatchSubscription::Build(
    const std::shared_ptr<CatchData> &data) {
  return std::make_shared<CatchSubscription>(data);
}

void CatchSubscription::Request(ElementCount count) {
  data_->requested += count;
  inner_subscription_.Request(count);
}

void CatchSubscription::Cancel() {
  data_->cancelled = true;
  inner_subscription_.Cancel();
}

}  // namespace detail
}  // namespace shk
