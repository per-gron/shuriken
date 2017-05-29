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

#include <rx/subscriber.h>
#include <rx/subscription.h>

namespace shk {

template <typename T, typename Publisher>
T get(const Publisher &publisher, size_t request_count = Subscription::kAll) {
  bool has_value = false;
  bool is_done = false;
  T result{};
  auto sub = publisher(MakeSubscriber(
      [&result, &has_value, &is_done](T &&val) {
        CHECK(!is_done);
        CHECK(!has_value);
        result = std::move(val);
        has_value = true;
      },
      [](std::exception_ptr &&error) { CHECK(!"should not happen"); },
      [&has_value, &is_done] {
        CHECK(!is_done);
        CHECK(has_value);
        is_done = true;
      }));
  CHECK(!has_value);
  CHECK(!is_done);
  sub.Request(request_count);
  CHECK(is_done == (request_count != 0));
  return result;
}

}  // namespace shk
