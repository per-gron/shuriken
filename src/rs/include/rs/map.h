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

#pragma once

#include <type_traits>

#include <rs/subscriber.h>
#include <rs/subscription.h>

namespace shk {
namespace detail {

template <typename InnerSubscriberType, typename Mapper>
class MapSubscriber : public SubscriberBase {
 public:
  MapSubscriber(InnerSubscriberType &&inner_subscriber, const Mapper &mapper)
      : inner_subscriber_(std::move(inner_subscriber)),
        mapper_(mapper) {}

  template <typename T>
  void OnNext(T &&t) {
    inner_subscriber_.OnNext(mapper_(std::forward<T>(t)));
  }

  void OnError(std::exception_ptr &&error) {
    inner_subscriber_.OnError(std::move(error));
  }

  void OnComplete() {
    inner_subscriber_.OnComplete();
  }

 private:
  InnerSubscriberType inner_subscriber_;
  Mapper mapper_;
};

}  // namespace detail

template <typename Mapper>
auto Map(Mapper &&mapper) {
  // Return an operator (it takes a Publisher and returns a Publisher)
  return [mapper = std::forward<Mapper>(mapper)](auto source) {
    // Return a Publisher
    return [mapper, source = std::move(source)](auto &&subscriber) {
      return source(detail::MapSubscriber<
          typename std::decay<decltype(subscriber)>::type,
          typename std::decay<Mapper>::type>(
              std::forward<decltype(subscriber)>(subscriber),
              mapper));
    };
  };
}

}  // namespace shk
