#pragma once

#include <string>
#include <vector>

namespace shk {

struct Step {
  /**
   * Other build steps that depend on this build step.
   */
  std::vector<const Step *> dependents;

  /**
   * The command string is empty for phony rules.
   */
  std::function<std::string ()> command;

  bool restat = false;

  /**
   * Input files, as specified in the manifest. These are used only as names
   * for targets, they are not actually used in the build process.
   */
  std::vector<Path> inputs;

  /**
   * Output files, as specified in the manifest. These are used as names for
   * targets and to make sure that the directory where the outputs should live
   * exists before the command is invoked.
   */
  std::vector<Path> outputs;

  Hash hash() const;
};

using Steps = std::unordered_map<Hash, Step>;

}  // namespace shk
