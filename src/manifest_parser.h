#pragma once

#include <string>

#include "file_system.h"
#include "step.h"

namespace shk {

Steps parseManifest(
    FileSystem &file_system,
    const std::string &path);

}  // namespace shk
