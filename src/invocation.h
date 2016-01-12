#pragma once

#include <vector>

#include "fingerprint.h"
#include "hash.h"

namespace shk {

struct Invocation {
  std::vector<Fingerprint> output_files;
  std::vector<Fingerprint> input_files;
  Hash build_step_hash = 0;
};

using Invocations = std::vector<Invocations>;

}  // namespace shk
