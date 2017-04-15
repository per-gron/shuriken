#pragma once

#include <unordered_map>
#include <vector>

#include "build_error.h"
#include "fs/path.h"
#include "manifest/raw_manifest.h"
#include "manifest/step.h"

namespace shk {

/**
 * Please note that this map contains only paths that are in the RawManifest; it
 * does not have output files that may have been created but that are not
 * declared.
 *
 * This map is configured to treat paths that are the same according to
 * Path::isSame as equal. This is important because otherwise the lookup
 * will miss paths that point to the same thing but with different original
 * path strings.
 */
using PathToStepMap = std::unordered_map<
    Path, StepIndex, Path::IsSameHash, Path::IsSame>;

/**
 * Similar to PathToStepMap, but is a sorted list of canonicalized paths along
 * with the associated StepIndex for each path.
 *
 * The paths are canonicalized without consulting the file system. This means
 * that these paths will be broken if there are symlinks around. These lists are
 * intended to be used for selecting build steps based on command line input,
 * not where correctness is required.
 */
using PathToStepList = std::vector<std::pair<std::string, StepIndex>>;

namespace detail {

/**
 * Throws BuildError if there exists an output file that more than one step
 * generates.
 */
PathToStepMap computeOutputPathMap(
    const std::vector<RawStep> &steps) throw(BuildError);

/**
 * Compute the "root steps," that is the steps that don't have an output that
 * is an input to some other step. This is the set of steps that are built if
 * there are no default statements in the manifest and no steps where
 * specifically requested to be built.
 *
 * If no step can be identified as root (perhaps because there is a cyclic
 * dependency), this function returns an empty vector.
 */
std::vector<StepIndex> rootSteps(
    const std::vector<Step> &steps) throw(BuildError);

/**
 * Generate a string that describes a cycle, for example "a -> b -> a".
 * cycle must be a non-empty vector.
 */
std::string cycleErrorMessage(const std::vector<Path> &cycle);

}  // namespace detail

/**
 * RawManifest objects contain information about the build that is structured in
 * a way that closely mirrors the manifest file itself: It has a list of build
 * steps. This is nice because it is close to what the input is like, but it is
 * not necessarily efficient to work with when actually building.
 *
 * IndexedManifest has all the information that the RawManifest has, plus some
 * info that makes is fast to look up things that are often used in a build,
 * including Step hashes and a output file Path => Step map.
 *
 * Computing an IndexedManifest from a RawManifest is a pure function. This
 * means that an IndexedManifest can be reused between different builds.
 */
struct IndexedManifest {
  IndexedManifest() = default;
  IndexedManifest(RawManifest &&manifest);

  /**
   * Map of path => index of the step that has this file as an output.
   */
  PathToStepMap output_path_map;

  /**
   * Associative list of path => index of the step that has this file as an
   * output.
   */
  PathToStepList outputs;

  /**
   * Associative list of path => index of the step that has this file as an
   * input.
   */
  PathToStepList inputs;

  std::vector<Step> steps;
  std::vector<StepIndex> defaults;
  std::vector<StepIndex> roots;
  std::unordered_map<std::string, int> pools;

  /**
   * The build directory, used for storing the invocation log.
   */
  std::string build_dir;

  /**
   * Is a non-empty string describing a cycle in the build graph if one exists.
   * For example: "a -> b -> a"
   */
  std::string dependency_cycle;
};

}  // namespace shk
