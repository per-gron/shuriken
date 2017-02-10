#pragma once

#include "status/build_status.h"

namespace shk {

class DummyBuildStatus : public BuildStatus {
 public:

  void stepStarted(const Step &step) override {}

  void stepFinished(
      const Step &step,
      bool success,
      const std::string &output) override {}
};

}  // namespace shk
