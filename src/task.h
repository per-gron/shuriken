#pragma once

#include <string>
#include <vector>

#include "fingerprint.h"

namespace shk {

struct Task {
  std::string command;
  std::vector<Fingerprint> prior_outputs;
  bool restat;
  // Other tasks that depend on this one
  std::vector<Task *> dependents;
  // The number of tasks that this task depends on
  int dependencies;
};

using Tasks = std::vector<Task>;

}  // namespace shk
