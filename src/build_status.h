#pragma once

namespace shk {

/**
 * A BuildStatus object lives for the duration of one build. It is the mechanism
 * in which the build process reports back what is going on. Its primary use
 * is to be able to show progress indication to the user (that is not something
 * the core build process is concerned with).
 */
class BuildStatus {
 public:
  virtual ~BuildStatus() = default;

  // TODO(peck): To be defined
};

}  // namespace shk
