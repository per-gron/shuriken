#pragma once

#include <unordered_map>
#include <vector>

#include "build_error.h"
#include "fs/path.h"
#include "manifest/raw_manifest.h"
#include "manifest/step.h"

namespace shk {

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
  CompiledManifest() = default;
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
  const PathToStepList &outputs() const {
    return _outputs;
  }

  /**
   * Associative list of path => index of the step that has this file as an
   * input.
   */
  const PathToStepList &inputs() const {
    return _inputs;
  }

  const std::vector<Step> &steps() const {
    return _steps;
  }

  const std::vector<StepIndex> &defaults() const {
    return _defaults;
  }

  const std::vector<StepIndex> &roots() const {
    return _roots;
  }

  const std::unordered_map<std::string, int> &pools() const {
    return _pools;
  }

  /**
   * The build directory, used for storing the invocation log.
   */
  nt_string_view buildDir() const {
    return _build_dir;
  }

  /**
   * Index of the build step that rebuilds the manifest file, or -1 if there is
   * no such step.
   */
  StepIndex manifestStep() const {
    return _manifest_step;
  }

  /**
   * Is a non-empty string describing a cycle in the build graph if one exists.
   * For example: "a -> b -> a"
   */
  nt_string_view dependencyCycle() const {
    return _dependency_cycle;
  }

 private:
  std::vector<std::unique_ptr<flatbuffers::FlatBufferBuilder>> _step_buffers;
  PathToStepList _outputs;
  PathToStepList _inputs;
  std::vector<Step> _steps;
  std::vector<StepIndex> _defaults;
  std::vector<StepIndex> _roots;
  std::unordered_map<std::string, int> _pools;
  std::string _build_dir;
  StepIndex _manifest_step = -1;
  std::string _dependency_cycle;
};

}  // namespace shk
