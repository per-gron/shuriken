#pragma once

#include <string>
#include <vector>

#include "hash.h"
#include "path.h"

namespace shk {

/**
 * A Step is a dumb data object that represents one build statment in the
 * build manifest.
 *
 * Parsing the build manifest and evaluating the rules results in a list of
 * Step objects. When the Steps object have been created, the manifest and the
 * variable environments etc can be discarded. It is not possible to recreate
 * the manifest from the list of steps; Step objects contain already evaluated
 * commands.
 */
struct Step {
  /**
   * Command that should be invoked in order to perform this build step.
   *
   * The command string is empty for phony rules.
   */
  std::string command;

  bool restat = false;

  /**
   * Input files, implicit depenencies and order only dependencies, as specified
   * in the manifest.
   *
   * Because the only difference between Ninja "explicit" and "implicit"
   * dependencies is that implicit dependencies don't show up in the $in
   * variable there is no need to distinguish between them in Step objects. The
   * command has already been evaluated so there is no point in differentiating
   * them anymore.
   *
   * Furthermore, because these input dependencies are used only in the first
   * build (subsequent builds use dependency information gathered from running
   * the command), there is no need to distinguish between explicit/implicit
   * dependencies and order-only dependencies.
   */
  std::vector<Path> dependencies;

  /**
   * Output files, as specified in the manifest. These are used as names for
   * targets and to make sure that the directory where the outputs should live
   * exists before the command is invoked.
   */
  std::vector<Path> outputs;
};

}  // namespace shk
