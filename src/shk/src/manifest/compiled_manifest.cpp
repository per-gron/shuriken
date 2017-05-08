// Copyright 2017 Per Grön. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "manifest/compiled_manifest.h"

#include <util/path_operations.h>

namespace shk {
namespace detail {

PathToStepMap computeOutputPathMap(
    const std::vector<RawStep> &steps) throw(BuildError) {
  PathToStepMap result;

  for (size_t i = 0; i < steps.size(); i++) {
    const auto &step = steps[i];
    for (const auto &output : step.outputs) {
      const auto ins = result.emplace(output, i);
      if (!ins.second && ins.first->second != i) {
        throw BuildError("Multiple rules generate " + output.original());
      }
    }
  }

  return result;
}

std::string cycleErrorMessage(const std::vector<Path> &cycle) {
  if (cycle.empty()) {
    // There can't be a cycle without any nodes. Then it's not a cycle...
    return "[internal error]";
  }

  std::string error;
  for (const auto &path : cycle) {
    error += path.original() + " -> ";
  }
  error += cycle.front().original();
  return error;
}

}  // namespace detail

namespace {

detail::PathToStepMap computeInputPathMap(
    const std::vector<RawStep> &steps) throw(BuildError) {
  detail::PathToStepMap result;

  auto process = [&](int idx, const std::vector<Path> &paths) {
    for (const auto path : paths) {
      result.emplace(path, idx);
    }
  };

  for (size_t i = 0; i < steps.size(); i++) {
    const auto &step = steps[i];

    process(i, step.inputs);
    process(i, step.implicit_inputs);
    process(i, step.dependencies);
  }

  return result;
}

template <typename CreateString>
std::vector<flatbuffers::Offset<
    ShkManifest::StepPathReference>> computePathList(
        flatbuffers::FlatBufferBuilder &builder,
        const CreateString &create_string,
        const detail::PathToStepMap &path_map) {
  std::vector<std::pair<std::string, StepIndex>> vec;
  for (const auto &path_pair : path_map) {
    auto path = path_pair.first.original();
    try {
      canonicalizePath(&path);
    } catch (const PathError &) {
      continue;
    }

    vec.emplace_back(std::move(path), path_pair.second);
  }

  std::sort(vec.begin(), vec.end());

  std::vector<flatbuffers::Offset<ShkManifest::StepPathReference>> result;
  result.reserve(vec.size());
  for (const auto &path_pair : vec) {
    result.push_back(ShkManifest::CreateStepPathReference(
        builder,
        create_string(path_pair.first),
        path_pair.second));
  }

  return result;
}

template <typename CreateString>
flatbuffers::Offset<ShkManifest::Step> convertRawStep(
    const detail::PathToStepMap &output_path_map,
    std::vector<bool> &roots,
    flatbuffers::FlatBufferBuilder &builder,
    const CreateString &create_string,
    const std::vector<RawStep> &steps,
    const RawStep &raw,
    std::string *err) {
  if (raw.generator && !raw.depfile.empty()) {
    // Disallow depfile + generator rules. Otherwise it would be necessary to
    // run the rule again just to get the deps, and we don't want to have to
    // re-run the manifest file generator on the first build.
    *err = "Generator build steps must not have depfile";
  }

  std::vector<StepIndex> dependencies;
  const auto process_inputs = [&](
      const std::vector<Path> &paths) {
    for (const auto &path : paths) {
      const auto it = output_path_map.find(path);
      if (it != output_path_map.end()) {
        auto dependency_idx = it->second;
        dependencies.push_back(dependency_idx);
        roots[dependency_idx] = false;
      }
    }
  };
  process_inputs(raw.inputs);
  process_inputs(raw.implicit_inputs);
  process_inputs(raw.dependencies);

  std::sort(dependencies.begin(), dependencies.end());
  dependencies.erase(
      std::unique(dependencies.begin(), dependencies.end()),
      dependencies.end());

  auto deps_vector = builder.CreateVector(
      dependencies.data(), dependencies.size());

  std::unordered_set<std::string> output_dirs_set;
  for (const auto &output : raw.outputs) {
    output_dirs_set.insert(std::string(dirname(output.original())));
  }

  std::vector<flatbuffers::Offset<flatbuffers::String>> output_dirs;
  output_dirs.reserve(output_dirs_set.size());
  for (auto &output : output_dirs_set) {
    if (output == ".") {
      continue;
    }
    output_dirs.push_back(create_string(output));
  }
  auto output_dirs_vector = builder.CreateVector(
      output_dirs.data(), output_dirs.size());

  auto pool_name_string = create_string(raw.pool_name);
  auto command_string = create_string(raw.command);
  auto description_string = create_string(raw.description);
  auto depfile_string = create_string(raw.depfile);
  auto rspfile_string = create_string(raw.rspfile);
  auto rspfile_content_string = create_string(raw.rspfile_content);

  using StringVectorOffset = flatbuffers::Offset<flatbuffers::Vector<
      flatbuffers::Offset<flatbuffers::String>>>;
  StringVectorOffset generator_inputs_vector;
  StringVectorOffset generator_outputs_vector;
  if (raw.generator) {
    std::vector<flatbuffers::Offset<flatbuffers::String>> generator_inputs;
    generator_inputs.reserve(dependencies.size());
    std::vector<flatbuffers::Offset<flatbuffers::String>> generator_outputs;
    generator_outputs.reserve(raw.outputs.size());

    const auto process_strings = [&](
        std::vector<flatbuffers::Offset<flatbuffers::String>> &vec,
        const std::vector<Path> &paths) {
      for (const auto &path : paths) {
        vec.push_back(create_string(path.original()));
      }
    };

    process_strings(generator_inputs, raw.inputs);
    process_strings(generator_inputs, raw.implicit_inputs);
    process_strings(generator_inputs, raw.dependencies);
    process_strings(generator_outputs, raw.outputs);

    generator_inputs_vector = builder.CreateVector(
        generator_inputs.data(), generator_inputs.size());
    generator_outputs_vector = builder.CreateVector(
        generator_outputs.data(), generator_outputs.size());
  }

  ShkManifest::StepBuilder step_builder(builder);
  step_builder.add_hash(
      reinterpret_cast<const ShkManifest::Hash *>(raw.hash().data.data()));
  step_builder.add_dependencies(deps_vector);
  step_builder.add_output_dirs(output_dirs_vector);
  step_builder.add_pool_name(pool_name_string);
  step_builder.add_command(command_string);
  step_builder.add_description(description_string);
  step_builder.add_depfile(depfile_string);
  step_builder.add_rspfile(rspfile_string);
  step_builder.add_rspfile_content(rspfile_content_string);
  step_builder.add_generator(raw.generator);
  if (raw.generator) {
    step_builder.add_generator_inputs(generator_inputs_vector);
    step_builder.add_generator_outputs(generator_outputs_vector);
  }

  return step_builder.Finish();
}

template <typename CreateString>
std::vector<flatbuffers::Offset<ShkManifest::Step>> convertStepVector(
    const detail::PathToStepMap &output_path_map,
    std::vector<bool> &roots,
    flatbuffers::FlatBufferBuilder &builder,
    const CreateString &create_string,
    const std::vector<RawStep> &steps,
    std::string *err) {
  std::vector<flatbuffers::Offset<ShkManifest::Step>> ans;
  ans.reserve(steps.size());

  for (const auto &step : steps) {
    ans.push_back(convertRawStep(
        output_path_map, roots, builder, create_string, steps, step, err));
    if (!err->empty()) {
      break;
    }
  }

  return ans;
}

std::vector<StepIndex> computeStepsToBuildFromPaths(
    const std::vector<Path> &paths,
    const detail::PathToStepMap &output_path_map) throw(BuildError) {
  std::vector<StepIndex> result;
  for (const auto &default_path : paths) {
    const auto it = output_path_map.find(default_path);
    if (it == output_path_map.end()) {
      throw BuildError(
          "Specified target does not exist: " + default_path.original());
    }
    // This may result in duplicate values in result, which is ok
    result.push_back(it->second);
  }
  return result;
}

bool hasDependencyCycle(
    const detail::PathToStepMap &output_path_map,
    const std::vector<RawStep> &raw_steps,
    std::vector<bool> &currently_visited,
    std::vector<bool> &already_visited,
    std::vector<Path> &cycle_paths,
    StepIndex idx,
    std::string *cycle) {
  if (currently_visited[idx]) {
    *cycle = detail::cycleErrorMessage(cycle_paths);
    return true;
  }

  if (already_visited[idx]) {
    // The step has already been processed.
    return false;
  }
  already_visited[idx] = true;

  bool found_cycle = false;
  const auto process_inputs = [&](const std::vector<Path> &inputs) {
    if (found_cycle) {
      return;
    }

    for (const auto &input : inputs) {
      const auto it = output_path_map.find(input);
      if (it == output_path_map.end()) {
        // This input is not an output of some other build step.
        continue;
      }

      const auto dependency_idx = it->second;

      cycle_paths.push_back(input);
      if (hasDependencyCycle(
              output_path_map,
              raw_steps,
              currently_visited,
              already_visited,
              cycle_paths,
              dependency_idx,
              cycle)) {
        found_cycle = true;
        return;
      }
      cycle_paths.pop_back();
    }
  };

  currently_visited[idx] = true;
  process_inputs(raw_steps[idx].inputs);
  process_inputs(raw_steps[idx].implicit_inputs);
  process_inputs(raw_steps[idx].dependencies);
  currently_visited[idx] = false;

  return found_cycle;
}

std::string getDependencyCycle(
    const detail::PathToStepMap &output_path_map,
    const std::vector<RawStep> &raw_steps) {
  std::vector<bool> currently_visited(raw_steps.size());
  std::vector<bool> already_visited(raw_steps.size());
  std::vector<Path> cycle_paths;
  cycle_paths.reserve(32);  // Guess at largest typical build dependency depth

  std::string cycle;
  for (StepIndex idx = 0; idx < raw_steps.size(); idx++) {
    if (hasDependencyCycle(
            output_path_map,
            raw_steps,
            currently_visited,
            already_visited,
            cycle_paths,
            idx,
            &cycle)) {
      break;
    }
  }

  return cycle;
}

/**
 * For each of the steps in the provided StepsView, this function returns the
 * index of a (possibly transitive) dependency for which the predicate returns
 * true.
 */
template <typename Predicate>
Optional<StepIndex> searchStepDependenciesHelper(
    StepsView steps,
    const Predicate &predicate,
    StepIndex idx,
    std::vector<bool> &already_visited,
    std::vector<Optional<StepIndex>> &result) {
  if (already_visited[idx]) {
    // The step has already been processed. Avoid unnecessary duplicate
    // computation for when the build graph forms a non-tree DAG and just quit.
    //
    // It is legal to take what's in the result array, because we will either be
    // taking the result of a step that has been fully computed, or we are in a
    // cycle and will get nothing. But in the case of a cycle the
    // searchStepDependencies function as a whole will be able to find a result
    // if there is one anyway.
    return result[idx];
  }
  already_visited[idx] = true;

  const auto step = steps[idx];
  if (predicate(step)) {
    return result[idx] = Optional<StepIndex>(idx);
  }
  for (const auto dependency_idx : step.dependencies()) {
    if (const auto result_index = searchStepDependenciesHelper(
            steps, predicate, dependency_idx, already_visited, result)) {
      return result[idx] = result_index;
    }
  }
  return Optional<StepIndex>();
}

/**
 * For each of the steps in the provided StepsView, this function returns the
 * index of a (possibly transitive) dependency for which the predicate returns
 * true.
 */
template <typename Predicate>
std::vector<Optional<StepIndex>> searchStepDependencies(
    StepsView steps,
    const Predicate &predicate) {
  std::vector<bool> already_visited(steps.size());
  std::vector<Optional<StepIndex>> result(steps.size());

  for (StepIndex idx = 0; idx < steps.size(); idx++) {
    searchStepDependenciesHelper(
        steps,
        predicate,
        idx,
        already_visited,
        result);
  }

  return result;
}

/**
 * Verify that there are no disallowed dependencies between generator build
 * steps and non-generator build steps. If there is an erroneous dependency,
 * return a non-empty error string.
 *
 * "normal" non-generator build steps are not allowed to depend on generator
 * build steps. Two reasons:
 *
 * 1) Because generator build steps have weaker correctness guarantees due
 * to their racy mtime cleanliness check, these weaker correctness
 * guarantees could spread to normal build steps too, which can have bad
 * consequences especially when caching things.
 *
 * 2) Because generator build steps are not logged in the invocation log,
 * the build process can't get the file ids of the outputs of generator
 * build steps like it can for others. The file ids are used after invoking
 * a build step to compute the ignored_dependencies and
 * additional_dependencies fields in the invocation log. If the map of file
 * ids is incomplete, the ignored/additional dependencies calculation does
 * not work.
 *
 * However, since ignored/additional dependencies only needs to be computed
 * for steps that are logged to the invocation log (that is: not generator
 * build steps), it does not matter if the file ids of generator step
 * outputs are not present if non generator build steps can't depend on
 * them.
 *
 * Also, generator build steps are not allowed to depend on non-generator build
 * steps. Reason:
 *
 * From a correctness perspective, there is not currently a reason to
 * disallow generator steps to depend on normal steps, but I'm still
 * disallowing it, for consistency reasons, and because it allows for
 * greater flexibility in the future: It is possible that generator step
 * handling will need to change for one reason or another, and keeping
 * generator vs non-generator steps as separate islands just makes it a lot
 * easier to reason about what's going on when changing build semantics.
 */
std::string getBadGeneratorDependency(StepsView steps) {
  std::vector<bool> already_visited(steps.size());
  auto generator_dependency = searchStepDependencies(steps, [](Step step) {
    return !step.phony() && step.generator();
  });
  auto non_generator_dependency = searchStepDependencies(steps, [](Step step) {
    return !step.phony() && !step.generator();
  });

  for (StepIndex idx = 0; idx < steps.size(); idx++) {
    if (steps[idx].phony()) {
      // Phony steps are allowed to depend on anything.
      continue;
    }
    const bool generator = steps[idx].generator();
    auto illegal_dependency_index = (generator ?
        non_generator_dependency : generator_dependency)[idx];
    if (illegal_dependency_index) {
      auto verbose_error =
            std::string(steps[idx].command()) +
            " depends on " +
            std::string(steps[*illegal_dependency_index].command());
      if (generator) {
        return
            "Generator build steps must not depend on normal build steps: " +
            verbose_error;
      } else {
        return
            "Normal build steps must not depend on generator build steps: " +
            verbose_error;
      }
    }
  }

  return "";
}

StepIndex getManifestStep(
    const detail::PathToStepMap &output_path_map,
    Path manifest_path) {
  auto step_it = output_path_map.find(manifest_path);
  if (step_it == output_path_map.end()) {
    return -1;
  } else {
    return step_it->second;
  }
}

}  // anonymous namespace

CompiledManifest::CompiledManifest(const ShkManifest::Manifest &manifest)
    : _manifest(&manifest) {}

Optional<CompiledManifest> CompiledManifest::load(
    string_view data, std::string *err) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const uint8_t *>(data.data()),
      data.size());
  if (!ShkManifest::VerifyManifestBuffer(verifier)) {
    *err = "Manifest file did not pass Flatbuffer validation";
    return Optional<CompiledManifest>();
  }

  const auto &manifest = *ShkManifest::GetManifest(data.data());

  int num_steps = manifest.steps() ? manifest.steps()->size() : 0;

  const auto fail_validation = [err]() {
    *err = "Encountered invalid step index";
    return Optional<CompiledManifest>();
  };

  const auto is_valid_index = [num_steps](StepIndex index) {
    return index >= 0 && index < num_steps;
  };

  auto compiled_manifest = CompiledManifest(manifest);

  StepPathReferencesView step_path_refs_list[] = {
      compiled_manifest.outputs(), compiled_manifest.inputs() };
  for (auto step_path_refs : step_path_refs_list) {
    for (auto ref : step_path_refs) {
      if (!is_valid_index(ref.second)) {
        return fail_validation();
      }
    }
  }

  for (auto step : compiled_manifest.steps()) {
    for (StepIndex step_index : step.dependencies()) {
      if (!is_valid_index(step_index)) {
        return fail_validation();
      }
    }
  }

  for (auto step_index : compiled_manifest.defaults()) {
    if (!is_valid_index(step_index)) {
      return fail_validation();
    }
  }

  for (auto step_index : compiled_manifest.roots()) {
    if (!is_valid_index(step_index)) {
      return fail_validation();
    }
  }

  for (auto pool : compiled_manifest.pools()) {
    if (pool.second < 0) {
      *err = "Encountered invalid pool depth";
      return Optional<CompiledManifest>();
    }
  }

  if (compiled_manifest.manifestStep() &&
      !is_valid_index(*compiled_manifest.manifestStep())) {
    return fail_validation();
  }

  return Optional<CompiledManifest>(compiled_manifest);
}

bool CompiledManifest::compile(
      flatbuffers::FlatBufferBuilder &builder,
      Path manifest_path,
      const RawManifest &manifest,
      std::string *err) {
  auto output_path_map = detail::computeOutputPathMap(manifest.steps);

  std::unordered_map<std::string, flatbuffers::Offset<flatbuffers::String>>
      string_memo;
  const auto create_string = [&](const std::string &str) {
    const auto it = string_memo.find(str);
    if (it == string_memo.end()) {
      return string_memo[str] = builder.CreateString(str.data(), str.size());
    } else {
      return it->second;
    }
  };

  auto outputs = computePathList(builder, create_string, output_path_map);
  auto outputs_vector = builder.CreateVector(
      outputs.data(), outputs.size());

  auto inputs = computePathList(builder, create_string, computeInputPathMap(manifest.steps));
  auto inputs_vector = builder.CreateVector(
      inputs.data(), inputs.size());

  // "Map" from StepIndex to whether the step is root or not.
  //
  // Assume that all steps are roots until we find some step that has an input
  // that is in a given step's list of outputs. Such steps are not roots.
  std::vector<bool> roots(manifest.steps.size(), true);

  auto steps = convertStepVector(
      output_path_map, roots, builder, create_string, manifest.steps, err);
  if (!err->empty()) {
    return false;
  }
  auto steps_vector = builder.CreateVector(
      steps.data(), steps.size());

  auto defaults = computeStepsToBuildFromPaths(
          manifest.defaults, output_path_map);
  auto defaults_vector = builder.CreateVector(
      defaults.data(), defaults.size());

  std::vector<StepIndex> root_step_indices;
  for (size_t i = 0; i < steps.size(); i++) {
    if (roots[i]) {
      root_step_indices.push_back(i);
    }
  }
  auto roots_vector = builder.CreateVector(
      root_step_indices.data(), root_step_indices.size());

  std::vector<flatbuffers::Offset<ShkManifest::Pool>> pools;
  for (const auto &pool : manifest.pools) {
    pools.push_back(ShkManifest::CreatePool(
        builder,
        create_string(pool.first),
        pool.second));
  }
  auto pools_vector = builder.CreateVector(
      pools.data(), pools.size());

  auto build_dir_string = create_string(manifest.build_dir);

  std::vector<flatbuffers::Offset<flatbuffers::String>> manifest_files;
  manifest_files.reserve(manifest.manifest_files.size());
  for (const auto &manifest_file : manifest.manifest_files) {
    manifest_files.push_back(create_string(manifest_file));
  }
  auto manifest_files_vector = builder.CreateVector(
      manifest_files.data(), manifest_files.size());

  auto cycle = getDependencyCycle(
      output_path_map,
      manifest.steps);
  if (!cycle.empty()) {
    *err = "Dependency cycle: " + cycle;
  }

  ShkManifest::ManifestBuilder manifest_builder(builder);
  manifest_builder.add_outputs(outputs_vector);
  manifest_builder.add_inputs(inputs_vector);
  manifest_builder.add_steps(steps_vector);
  manifest_builder.add_defaults(defaults_vector);
  manifest_builder.add_roots(roots_vector);
  manifest_builder.add_pools(pools_vector);
  manifest_builder.add_build_dir(build_dir_string);
  manifest_builder.add_manifest_step(
      getManifestStep(output_path_map, manifest_path));
  manifest_builder.add_manifest_files(manifest_files_vector);
  builder.Finish(manifest_builder.Finish());

  const auto compiled_manifest = CompiledManifest(*ShkManifest::GetManifest(
      builder.GetBufferPointer()));
  auto bad_generator_dependency =
      getBadGeneratorDependency(compiled_manifest.steps());
  if (!bad_generator_dependency.empty()) {
    *err = bad_generator_dependency;
    return false;
  }

  return err->empty();
}

template <typename Callback>
Optional<time_t> foldMtime(
    FileSystem &file_system, StringsView files, Callback &&cb) {
  Optional<time_t> ans;
  for (auto file : files) {
    auto stat = file_system.stat(file);
    if (stat.result != 0) {
      return Optional<time_t>();
    }
    ans = ans ? cb(*ans, stat.mtime) : stat.mtime;
  }
  return ans;
}

Optional<time_t> CompiledManifest::maxMtime(
    FileSystem &file_system, StringsView files) {
  return foldMtime(file_system, files, [](time_t a, time_t b) {
    return std::max(a, b);
  });
}

Optional<time_t> CompiledManifest::minMtime(
    FileSystem &file_system, StringsView files) {
  return foldMtime(file_system, files, [](time_t a, time_t b) {
    return std::min(a, b);
  });
}

namespace {

constexpr uint64_t kCompiledManifestVersion = 1;

std::pair<Optional<CompiledManifest>, std::shared_ptr<void>>
    loadPrecompiledManifest(
        FileSystem &file_system,
        const std::string &compiled_manifest_path,
        std::string *err) {
  const auto compiled_stat = file_system.stat(compiled_manifest_path);
  if (compiled_stat.result == ENOENT) {
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  }

  auto buffer = std::make_shared<std::string>();
  IoError error;
  std::tie(*buffer, error) = file_system.readFile(compiled_manifest_path);
  if (error) {
    // A more severe error than just a missing file is treated as an error,
    // for example if the path points to a directory.
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  }

  if (buffer->size() < sizeof(kCompiledManifestVersion)) {
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  }
  const auto version =
      flatbuffers::EndianScalar(
          *reinterpret_cast<const decltype(kCompiledManifestVersion) *>(
              buffer->data()));
  if (version != kCompiledManifestVersion) {
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  }

  std::string discard_err;
  auto manifest = CompiledManifest::load(
      string_view(
          buffer->data() + sizeof(version),
          buffer->size() - sizeof(version)),
      &discard_err);
  if (!manifest) {
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  }

  auto input_mtime = CompiledManifest::maxMtime(
      file_system, manifest->manifestFiles());

  if (!input_mtime || *input_mtime >= compiled_stat.mtime) {
    // The compiled manifest is out of date or has equal timestamps, which means
    // we don't know if it's out of date or not. Recompile just to be sure.
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  }

  return std::make_pair(Optional<CompiledManifest>(manifest), buffer);
}

}  // anonymous namespace

std::pair<Optional<CompiledManifest>, std::shared_ptr<void>>
    CompiledManifest::parseAndCompile(
        FileSystem &file_system,
        const std::string &manifest_path,
        const std::string &compiled_manifest_path,
        std::string *err) {
  auto precompiled = loadPrecompiledManifest(
      file_system, compiled_manifest_path, err);
  if (precompiled.first) {
    return precompiled;
  } else if (!err->empty()) {
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  }

  Paths paths(file_system);
  RawManifest raw_manifest;
  try {
    raw_manifest = parseManifest(paths, file_system, manifest_path);
  } catch (const IoError &io_error) {
    *err = std::string("failed to read manifest: ") + io_error.what();
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  } catch (const ParseError &parse_error) {
    *err = std::string("failed to parse manifest: ") + parse_error.what();
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  }

  flatbuffers::FlatBufferBuilder fb_builder(128 * 1024);
  if (!CompiledManifest::compile(
          fb_builder, paths.get(manifest_path), raw_manifest, err)) {
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  }

  auto buffer = std::make_shared<std::vector<char>>(
      reinterpret_cast<const char *>(fb_builder.GetBufferPointer()),
      reinterpret_cast<const char *>(
          fb_builder.GetBufferPointer() + fb_builder.GetSize()));
  auto buffer_view = string_view(buffer->data(), buffer->size());

  std::unique_ptr<FileSystem::Stream> stream;
  IoError error;
  std::tie(stream, error) = file_system.open(compiled_manifest_path, "wb");
  if (error) {
    *err = std::string("failed to write compiled manifest: ") + error.what();
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  }

  const auto version = flatbuffers::EndianScalar(kCompiledManifestVersion);
  if (auto error = stream->write(
          reinterpret_cast<const uint8_t *>(&version),
          1,
          sizeof(version))) {
    *err = std::string("failed to write compiled manifest: ") + error.what();
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  }

  if (auto error = stream->write(
          reinterpret_cast<const uint8_t *>(buffer_view.data()),
          1,
          buffer_view.size())) {
    *err = std::string("failed to write compiled manifest: ") + error.what();
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  }

  const auto maybe_manifest = CompiledManifest::load(buffer_view, err);
  if (!maybe_manifest) {
    return std::make_pair(Optional<CompiledManifest>(), nullptr);
  }

  return std::make_pair(maybe_manifest, buffer);
}

}  // namespace shk
