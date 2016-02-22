#pragma once

#include <functional>

#include <sys/time.h>

namespace shk {

using Clock = std::function<time_t ()>;

}  // namespace shk
