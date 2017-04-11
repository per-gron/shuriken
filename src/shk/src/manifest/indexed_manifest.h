#pragma once

#include <unordered_map>
#include <vector>

#include "build_error.h"
#include "fs/path.h"
#include "manifest/raw_manifest.h"
#include "manifest/step.h"

namespace shk {

/**
 * RawManifest objects contain an std::vector<Step>. A StepIndex is an index
 * into that vector, or into a vector of the same length that refers to the same
 * Step objects (for example PathToStepMap).
 */
using StepIndex = size_t;

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
    const std::vector<Step> &steps,
    const PathToStepMap &output_path_map) throw(BuildError);

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
   * Map of path => index of a step that has this file as an input. If there are
   * more than one, the map will contain one of them, chosen arbitrarily.
   */
  PathToStepMap input_path_map;

  std::vector<Step> steps;
  std::vector<StepIndex> defaults;
  std::vector<StepIndex> roots;
  std::unordered_map<std::string, int> pools;

  /**
   * The build directory, used for storing the invocation log.
   */
  std::string build_dir;
};

}  // namespace shk
