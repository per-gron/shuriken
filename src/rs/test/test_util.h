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

#include <vector>

#include <rs/subscriber.h>
#include <rs/subscription.h>

namespace shk {

template <typename T, typename Publisher>
T GetOne(
    const Publisher &publisher,
    size_t request_count = Subscription::kAll) {
  bool has_value = false;
  bool is_done = false;
  T result{};
  auto sub = publisher.Subscribe(MakeSubscriber(
      [&result, &has_value, &is_done](T &&val) {
        CHECK(!is_done);
        CHECK(!has_value);
        result = std::move(val);
        has_value = true;
      },
      [](std::exception_ptr &&error) {
        CHECK(!"OnError should not be called");
      },
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

template <typename T, typename Publisher>
std::vector<T> GetAll(
    const Publisher &publisher,
    size_t request_count = Subscription::kAll,
    bool expect_done = true) {
  std::vector<T> result;
  bool is_done = false;
  auto sub = publisher.Subscribe(MakeSubscriber(
      [&is_done, &result](auto &&val) {
        CHECK(!is_done);
        result.emplace_back(std::forward<decltype(val)>(val));
      },
      [](std::exception_ptr &&error) {
        CHECK(!"OnError should not be called");
      },
      [&is_done] {
        CHECK(!is_done);
        is_done = true;
      }));
  sub.Request(request_count);
  CHECK(is_done == expect_done);

  return result;
}

template <typename Publisher>
std::exception_ptr GetError(
    const Publisher &stream,
    size_t request_count = Subscription::kAll) {
  std::exception_ptr received_error;

  auto sub = stream.Subscribe(MakeSubscriber(
      [&received_error](int next) {
        CHECK(!received_error);
      },
      [&received_error](std::exception_ptr &&error) {
        CHECK(!received_error);
        received_error = error;
      },
      [] { CHECK(!"OnComplete should not be called"); }));
  sub.Request(request_count);
  CHECK(received_error);
  return received_error ?
      received_error :
      std::make_exception_ptr(
          std::logic_error("[no error when one was expected]"));
}

inline std::string GetErrorWhat(const std::exception_ptr &error) {
  if (!error) {
    return "[null error]";
  }
  try {
    std::rethrow_exception(error);
  } catch (const std::exception &error) {
    return error.what();
  }
}

}  // namespace shk