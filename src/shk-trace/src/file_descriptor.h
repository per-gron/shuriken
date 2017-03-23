#pragma once

#include <util/raii_helper.h>

namespace shk {
namespace detail {

void closeFd(int fd);

}  // namespace detail

using FileDescriptor = RAIIHelper<int, void, detail::closeFd, -1>;

}  // namespace shk
