#pragma once

#include <string>
#include <vector>

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
   * Order only dependencies, as specified in the manifest.
   *
   * In Shuriken, the "explicit" and "implicit" dependencies are not used for
   * dependency purposes; such dependencies are inferred from the syscalls that
   * the command made when it was run. order_only_dependencies is the only thing
   * from the build manifest that has an influence on the actual build DAG.
   */
  std::vector<Path> order_only_dependencies;

  /**
   * Input files, as specified in the manifest. These include both input files
   * (aka "explicit" dependencies) and "implicit" dependencies.
   *
   * Because the only difference between Ninja "explicit" and "implicit"
   * dependencies is that implicit dependencies don't show up in the $in
   * variable there is no need to distinguish between them in Step objects. The
   * command has already been evaluated so there is no point in differentiating
   * them anymore.
   */
  std::vector<Path> inputs;

  /**
   * Output files, as specified in the manifest. These are used as names for
   * targets and to make sure that the directory where the outputs should live
   * exists before the command is invoked.
   */
  std::vector<Path> outputs;
};

using Steps = std::unordered_map<Hash, Step>;

}  // namespace shk
