#pragma once

#include <dispatch/dispatch.h>

#include <util/raii_helper.h>

namespace shk {
namespace detail {

template <typename DispatchObject>
static void releaseDispatchObject(DispatchObject object) {
  dispatch_release(object);
}

}

using DispatchSource = RAIIHelper<
    dispatch_source_t, void, detail::releaseDispatchObject>;

using DispatchQueue = RAIIHelper<
    dispatch_queue_t, void, detail::releaseDispatchObject>;

}  // namespace shk
