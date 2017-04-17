#pragma once

#include <unordered_map>
#include <vector>

#include "build_error.h"
#include "fs/path.h"
#include "manifest/raw_manifest.h"
#include "manifest/step.h"

namespace shk {
namespace detail {

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
 * Throws BuildError if there exists an output file that more than one step
 * generates.
 */
PathToStepMap computeOutputPathMap(
    const std::vector<RawStep> &steps) throw(BuildError);

/**
 * Generate a string that describes a cycle, for example "a -> b -> a".
 * cycle must be a non-empty vector.
 */
std::string cycleErrorMessage(const std::vector<Path> &cycle);

inline std::pair<nt_string_view, StepIndex> fbStepPathReferenceToView(
    const ShkManifest::StepPathReference *ref) {
  return std::make_pair(fbStringToView(ref->path()), ref->step());
}

inline std::pair<nt_string_view, int> fbPoolToView(
    const ShkManifest::Pool *pool) {
  return std::make_pair(fbStringToView(pool->name()), pool->depth());
}

using FbStepPathReferenceIterator = flatbuffers::VectorIterator<
    flatbuffers::Offset<ShkManifest::StepPathReference>,
    const ShkManifest::StepPathReference *>;

using FbPoolIterator = flatbuffers::VectorIterator<
    flatbuffers::Offset<ShkManifest::Pool>,
    const ShkManifest::Pool *>;

}  // namespace detail

/**
 * A sorted list of canonicalized paths along with the associated StepIndex for
 * each path.
 *
 * The paths are canonicalized without consulting the file system. This means
 * that these paths will be broken if there are symlinks around. These lists are
 * intended to be used for selecting build steps based on command line input,
 * not where correctness is required.
 */
using StepPathReferencesView = WrapperView<
    detail::FbStepPathReferenceIterator,
    std::pair<nt_string_view, StepIndex>,
    &detail::fbStepPathReferenceToView>;

/**
 * View of an associative list of pool name to pool depth.
 */
using PoolsView = WrapperView<
    detail::FbPoolIterator,
    std::pair<nt_string_view, int>,
    &detail::fbPoolToView>;

namespace detail {

inline StepPathReferencesView toStepPathReferencesView(
    const flatbuffers::Vector<flatbuffers::Offset<
        ShkManifest::StepPathReference>> *refs) {
  return refs ?
      StepPathReferencesView(refs->begin(), refs->end()) :
      StepPathReferencesView(
          detail::FbStepPathReferenceIterator(nullptr, 0),
          detail::FbStepPathReferenceIterator(nullptr, 0));
}

inline PoolsView toPoolsView(
    const flatbuffers::Vector<flatbuffers::Offset<
        ShkManifest::Pool>> *pools) {
  return pools ?
      PoolsView(pools->begin(), pools->end()) :
      PoolsView(
          detail::FbPoolIterator(nullptr, 0),
          detail::FbPoolIterator(nullptr, 0));
}

}  // namespace detail

/**
 * A CompiledManifest is a build.ninja file compiled down to the bare
 * essentials. Its purpose is to avoid most of the processing that is involved
 * when parsing a build.ninja file:
 *
 * * It is a binary Flatbuffer, so no parsing of the file is needed
 * * Paths are already normalized, so no stat-ing or canonicalization has to
 *   be done.
 * * All string interpolation has already been performed
 * * Circular dependencies are caught in the compilation stage
 * * Dependencies are expressed directly as int indices, no hash lookups
 *   required there.
 *
 * In a way, a Manifest object is even more purely a build DAG than the
 * build.ninja file is.
 */
struct CompiledManifest {
  CompiledManifest(
      Path manifest_path,
      RawManifest &&manifest);

 private:
  CompiledManifest(
      const detail::PathToStepMap &output_path_map,
      Path manifest_path,
      RawManifest &&manifest);
 public:

  /**
   * Associative list of path => index of the step that has this file as an
   * output.
   */
  StepPathReferencesView outputs() const {
    return detail::toStepPathReferencesView(_manifest->outputs());
  }

  /**
   * Associative list of path => index of the step that has this file as an
   * input.
   */
  StepPathReferencesView inputs() const {
    return detail::toStepPathReferencesView(_manifest->inputs());
  }

  const std::vector<Step> &steps() const {
    return _steps;
  }

  StepIndicesView defaults() const {
    return detail::toStepIndicesView(_manifest->defaults());
  }

  StepIndicesView roots() const {
    return detail::toStepIndicesView(_manifest->roots());
  }

  PoolsView pools() const {
    return detail::toPoolsView(_manifest->pools());
  }

  /**
   * The build directory, used for storing the invocation log.
   */
  nt_string_view buildDir() const {
    return detail::toStringView(_manifest->build_dir());
  }

  /**
   * Index of the build step that rebuilds the manifest file, or -1 if there is
   * no such step.
   */
  StepIndex manifestStep() const {
    return _manifest->manifest_step();
  }

  /**
   * Is a non-empty string describing a cycle in the build graph if one exists.
   * For example: "a -> b -> a"
   */
  nt_string_view dependencyCycle() const {
    return detail::toStringView(_manifest->dependency_cycle());
  }

 private:
  std::shared_ptr<flatbuffers::FlatBufferBuilder> _builder;
  std::vector<std::unique_ptr<flatbuffers::FlatBufferBuilder>> _step_buffers;
  std::vector<Step> _steps;
  const ShkManifest::Manifest *_manifest;
};

}  // namespace shk
