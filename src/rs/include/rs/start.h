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

#include <rs/subscription.h>

namespace shk {

template <typename CreateValue>
auto Start(CreateValue &&create_value) {
  return [create_value = std::forward<CreateValue>(create_value)](
      auto &&subscriber) {
    return MakeSubscription(
        [
            create_value,
            subscriber = std::forward<decltype(subscriber)>(subscriber),
            sent = false](size_t count) mutable {
          if (!sent && count != 0) {
            sent = true;
            subscriber.OnNext(create_value());
            subscriber.OnComplete();
          }
        });
  };
}

}  // namespace shk
