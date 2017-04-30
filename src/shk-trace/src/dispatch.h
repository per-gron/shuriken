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

#include <dispatch/dispatch.h>

#include <util/raii_helper.h>

namespace shk {
namespace detail {

template <typename DispatchObject>
void releaseDispatchObject(DispatchObject object) {
  dispatch_release(object);
}

}  // namespace detail

using DispatchSource = RAIIHelper<
    dispatch_source_t, void, detail::releaseDispatchObject>;

using DispatchQueue = RAIIHelper<
    dispatch_queue_t, void, detail::releaseDispatchObject>;

using DispatchSemaphore = RAIIHelper<
    dispatch_semaphore_t, void, detail::releaseDispatchObject>;

}  // namespace shk
