#pragma once

#include <memory>

#include "build_status.h"

namespace shk {

/**
 * Create a BuildStatus object that reports the build status to the terminal.
 *
 * This is the main BuildStatus implementation.
 */
std::unique_ptr<BuildStatus> makeTerminalBuildStatus();

}  // namespace shk
