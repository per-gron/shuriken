#include "manifest/compiled_manifest.h"

namespace shk {
namespace detail {

PathToStepMap computeOutputPathMap(
    const std::vector<RawStep> &steps) throw(BuildError) {
  PathToStepMap result;

  for (size_t i = 0; i < steps.size(); i++) {
    const auto &step = steps[i];
    for (const auto &output : step.outputs) {
      const auto ins = result.emplace(output, i);
      if (!ins.second) {
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

std::vector<flatbuffers::Offset<
    ShkManifest::StepPathReference>> computePathList(
        flatbuffers::FlatBufferBuilder &builder,
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
        builder.CreateString(path_pair.first),
        path_pair.second));
  }

  return result;
}

flatbuffers::Offset<ShkManifest::Step> convertRawStep(
    const detail::PathToStepMap &output_path_map,
    std::vector<bool> &roots,
    flatbuffers::FlatBufferBuilder &builder,
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

  auto deps_vector = builder.CreateVector(
      dependencies.data(), dependencies.size());

  std::unordered_set<std::string> output_dirs_set;
  for (const auto &output : raw.outputs) {
    output_dirs_set.insert(dirname(output.original()));
  }

  std::vector<flatbuffers::Offset<flatbuffers::String>> output_dirs;
  output_dirs.reserve(output_dirs_set.size());
  for (auto &output : output_dirs_set) {
    if (output == ".") {
      continue;
    }
    output_dirs.push_back(builder.CreateString(output));
  }
  auto output_dirs_vector = builder.CreateVector(
      output_dirs.data(), output_dirs.size());

  auto pool_name_string = builder.CreateString(raw.pool_name);
  auto command_string = builder.CreateString(raw.command);
  auto description_string = builder.CreateString(raw.description);
  auto depfile_string = builder.CreateString(raw.depfile);
  auto rspfile_string = builder.CreateString(raw.rspfile);
  auto rspfile_content_string = builder.CreateString(raw.rspfile_content);

  flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<
        flatbuffers::String>>> generator_inputs_vector;
  if (raw.generator) {
    std::vector<flatbuffers::Offset<flatbuffers::String>> generator_inputs;
    generator_inputs.reserve(generator_inputs.size());

    const auto process_generator_inputs = [&](
        const std::vector<Path> &paths) {
      for (const auto &path : paths) {
        generator_inputs.push_back(builder.CreateString(path.original()));
      }
    };

    process_generator_inputs(raw.inputs);
    process_generator_inputs(raw.implicit_inputs);
    process_generator_inputs(raw.dependencies);

    generator_inputs_vector = builder.CreateVector(
        generator_inputs.data(), generator_inputs.size());
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
  }

  return step_builder.Finish();
}

std::vector<flatbuffers::Offset<ShkManifest::Step>> convertStepVector(
    const detail::PathToStepMap &output_path_map,
    std::vector<bool> &roots,
    flatbuffers::FlatBufferBuilder &builder,
    const std::vector<RawStep> &steps,
    std::string *err) {
  std::vector<flatbuffers::Offset<ShkManifest::Step>> ans;
  ans.reserve(steps.size());

  for (const auto &step : steps) {
    ans.push_back(convertRawStep(
        output_path_map, roots, builder, step, err));
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

  auto outputs = computePathList(builder, output_path_map);
  auto outputs_vector = builder.CreateVector(
      outputs.data(), outputs.size());

  auto inputs = computePathList(builder, computeInputPathMap(manifest.steps));
  auto inputs_vector = builder.CreateVector(
      inputs.data(), inputs.size());

  // "Map" from StepIndex to whether the step is root or not.
  //
  // Assume that all steps are roots until we find some step that has an input
  // that is in a given step's list of outputs. Such steps are not roots.
  std::vector<bool> roots(manifest.steps.size(), true);

  auto steps = convertStepVector(
      output_path_map, roots, builder, manifest.steps, err);
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
        builder.CreateString(pool.first),
        pool.second));
  }
  auto pools_vector = builder.CreateVector(
      pools.data(), pools.size());

  auto build_dir_string = builder.CreateString(manifest.build_dir);

  std::vector<flatbuffers::Offset<flatbuffers::String>> manifest_files;
  manifest_files.reserve(manifest.manifest_files.size());
  for (const auto &manifest_file : manifest.manifest_files) {
    manifest_files.push_back(builder.CreateString(manifest_file));
  }
  auto manifest_files_vector = builder.CreateVector(
      manifest_files.data(), manifest_files.size());

  auto cycle = getDependencyCycle(
      output_path_map,
      manifest.steps);
  if (!cycle.empty()) {
    *err = "Dependency cycle: "+ cycle;
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

  return err->empty();
}

}  // namespace shk
