#pragma once

#include <unordered_map>
#include <vector>

#include "build_error.h"
#include "fs/path.h"
#include "manifest.h"

namespace shk {

/**
 * Manifest objects contain an std::vector<Step>. A StepIndex is an index into
 * that vector, or into a vector of the same length that refers to the same
 * Step objects (for example StepHashes).
 */
using StepIndex = size_t;

/**
 * Map of path => index of the step that has this file as an output.
 *
 * Please note that this map contains only files that are in the Manifest; it
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

/**
 * "Map" of StepIndex => Hash of that step. The hash includes everything about
 * that step but not information about its dependencies.
 */
using StepHashes = std::vector<Hash>;

namespace detail {

/**
 * Throws BuildError if there exists an output file that more than one step
 * generates.
 */
OutputFileMap computeOutputFileMap(
    const std::vector<Step> &steps) throw(BuildError);

StepHashes computeStepHashes(const std::vector<Step> &steps);

}  // namespace detail

/**
 * Manifest objects contain information about the build that is structured in a
 * way that closely mirrors the manifest file itself: It has a list of build
 * steps. This is nice because it is close to what the input is like, but it is
 * not necessarily efficient to work with when actually building.
 *
 * IndexedManifest has all the information that the Manifest has, plus some info
 * that makes is fast to look up things that are often used in a build,
 * including Step hashes and a output file Path => Step map.
 *
 * Computing an IndexedManifest from a Manifest is a pure function. This means
 * that an IndexedManifest can be reused between different builds.
 */
struct IndexedManifest {
  IndexedManifest() = default;
  IndexedManifest(Manifest &&manifest);

  OutputFileMap output_file_map;
  StepHashes step_hashes;
  Manifest manifest;
};

}  // namespace shk
