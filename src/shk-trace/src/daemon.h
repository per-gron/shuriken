#pragma once

#include <functional>
#include <stdexcept>
#include <string>

struct DaemonConfig {
  std::string stdin = "/dev/null";
  std::string stdout = "/dev/null";
  std::string stderr = "/dev/null";
};

/**
 * Run the provided lambda in a daemonized process.
 */
void daemon(
    const DaemonConfig &config,
    const std::function<void ()> &run) throw(std::runtime_error);
