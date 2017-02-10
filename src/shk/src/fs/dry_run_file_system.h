#pragma once

#include "fs/file_system.h"

namespace shk {

/**
 * Create a file system that doesn't do anything on file modification
 * operations, it just silently ignores them.
 */
std::unique_ptr<FileSystem> dryRunFileSystem(FileSystem &inner_file_system);

}  // namespace shk
