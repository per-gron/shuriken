#pragma once

#include <string>

#include "manifest/step.h"

namespace shk {

/**
 * A BuildStatus object lives for the duration of one build. It is the mechanism
 * in which the build process reports back what is going on. Its primary use
 * is to be able to show progress indication to the user (that is not something
 * the core build process is concerned with).
 *
 * A BuildStatus is typically created when steps to perform has been planned and
 * counted. The BuildStatus object is destroyed when the build is done. The
 * destructor is a good place to do final reporting.
 */
class BuildStatus {
 public:
  virtual ~BuildStatus() = default;

  virtual void stepStarted(const Step &step) = 0;

  virtual void stepFinished(
      const Step &step,
      bool success,
      const std::string &output) = 0;
};

}  // namespace shk
