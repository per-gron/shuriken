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
 * Step objects (for example OutputFileMap).
 */
using StepIndex = size_t;

/**
 * Map of path => index of the step that has this file as an output.
 *
 * Please note that this map contains only files that are in the RawManifest; it
 * does not have output files that may have been created but that are not
 * declared.
 *
 * This is useful for traversing the build graph in the direction of a build
 * step to a build step that it depends on.
 *
 * This map is configured to treat paths that are the same according to
 * Path::isSame as equal. This is important because otherwise the lookup
 * will miss paths that point to the same thing but with different original
 * path strings.
 */
using OutputFileMap = std::unordered_map<
    Path, StepIndex, Path::IsSameHash, Path::IsSame>;

namespace detail {

/**
 * Throws BuildError if there exists an output file that more than one step
 * generates.
 */
OutputFileMap computeOutputFileMap(
    const std::vector<RawStep> &steps) throw(BuildError);

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

  OutputFileMap output_file_map;

  std::vector<Step> steps;
  std::vector<StepIndex> defaults;
  std::unordered_map<std::string, int> pools;

  /**
   * The build directory, used for storing the invocation log.
   */
  std::string build_dir;
};

}  // namespace shk
