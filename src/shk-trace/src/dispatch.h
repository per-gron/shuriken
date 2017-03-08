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
