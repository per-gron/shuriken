#pragma once

#include "file_system.h"

namespace shk {

std::unique_ptr<FileSystem> persistentFileSystem();

}  // namespace shk
