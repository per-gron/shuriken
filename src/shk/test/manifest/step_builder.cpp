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

#include "step_builder.h"

namespace shk {

StepBuilder &StepBuilder::setHash(Hash hash) {
  _hash = std::move(hash);
  return *this;
}

StepBuilder &StepBuilder::setDependencies(
    std::vector<StepIndex> dependencies) {
  _dependencies = std::move(dependencies);
  return *this;
}

StepBuilder &StepBuilder::setOutputDirs(
    std::vector<std::string> output_dirs) {
  _output_dirs = std::move(output_dirs);
  return *this;
}

StepBuilder &StepBuilder::setPoolName(std::string pool_name) {
  _pool_name = std::move(pool_name);
  return *this;
}

StepBuilder &StepBuilder::setCommand(std::string command) {
  _command = std::move(command);
  return *this;
}

StepBuilder &StepBuilder::setDescription(std::string description) {
  _description = std::move(description);
  return *this;
}

StepBuilder &StepBuilder::setDepfile(std::string depfile) {
  _depfile = std::move(depfile);
  return *this;
}

StepBuilder &StepBuilder::setRspfile(std::string rspfile) {
  _rspfile = std::move(rspfile);
  return *this;
}

StepBuilder &StepBuilder::setRspfileContent(std::string rspfile_content) {
  _rspfile_content = std::move(rspfile_content);
  return *this;
}

StepBuilder &StepBuilder::setGenerator(bool generator) {
  _generator = std::move(generator);
  return *this;
}

StepBuilder &StepBuilder::setGeneratorInputs(
    std::vector<std::string> generator_inputs) {
  _generator_inputs = std::move(generator_inputs);
  return *this;
}

StepBuilder &StepBuilder::setGeneratorOutputs(
    std::vector<std::string> generator_outputs) {
  _generator_outputs = std::move(generator_outputs);
  return *this;
}

Step StepBuilder::build(flatbuffers::FlatBufferBuilder &builder) {
  auto deps_vector = builder.CreateVector(
      _dependencies.data(), _dependencies.size());

  const auto to_string_vector = [&](const std::vector<std::string> &strs) {
    std::vector<flatbuffers::Offset<flatbuffers::String>> offsets;
    offsets.reserve(strs.size());
    for (const auto &str : strs) {
      offsets.push_back(builder.CreateString(str));
    }
    return builder.CreateVector(offsets.data(), offsets.size());
  };

  auto output_dirs_vector = to_string_vector(_output_dirs);
  auto pool_name_string = builder.CreateString(_pool_name);
  auto command_string = builder.CreateString(_command);
  auto description_string = builder.CreateString(_description);
  auto depfile_string = builder.CreateString(_depfile);
  auto rspfile_string = builder.CreateString(_rspfile);
  auto rspfile_content_string = builder.CreateString(_rspfile_content);
  auto generator_inputs_vector = to_string_vector(_generator_inputs);
  auto generator_outputs_vector = to_string_vector(_generator_outputs);

  ShkManifest::StepBuilder step_builder(builder);
  step_builder.add_hash(
      reinterpret_cast<const ShkManifest::Hash *>(_hash.data.data()));
  step_builder.add_dependencies(deps_vector);
  step_builder.add_output_dirs(output_dirs_vector);
  step_builder.add_pool_name(pool_name_string);
  step_builder.add_command(command_string);
  step_builder.add_description(description_string);
  step_builder.add_depfile(depfile_string);
  step_builder.add_rspfile(rspfile_string);
  step_builder.add_rspfile_content(rspfile_content_string);
  step_builder.add_generator(_generator);
  step_builder.add_generator_inputs(generator_inputs_vector);
  step_builder.add_generator_outputs(generator_outputs_vector);
  builder.Finish(step_builder.Finish());

  return Step(*flatbuffers::GetRoot<ShkManifest::Step>(
      builder.GetBufferPointer()));
}

StepBuilder StepBuilder::fromStep(const Step &step) {
  StepBuilder builder;
  builder.setHash(Hash(step.hash()));

  std::vector<StepIndex> deps(step.dependencies().size());
  std::copy(
      step.dependencies().begin(),
      step.dependencies().end(),
      deps.begin());
  builder.setDependencies(std::move(deps));

  const auto to_string_vector = [](StringsView view) {
    std::vector<std::string> ans;
    ans.reserve(view.size());
    for (const auto str : view) {
      ans.emplace_back(str);
    }
    return ans;
  };

  builder.setOutputDirs(to_string_vector(step.outputDirs()));
  builder.setPoolName(std::string(step.poolName()));
  builder.setCommand(std::string(step.command()));
  builder.setDescription(std::string(step.description()));
  builder.setDepfile(std::string(step.depfile()));
  builder.setRspfile(std::string(step.rspfile()));
  builder.setRspfileContent(std::string(step.rspfileContent()));
  builder.setGenerator(step.generator());
  builder.setGeneratorInputs(to_string_vector(step.generatorInputs()));
  builder.setGeneratorOutputs(to_string_vector(step.generatorOutputs()));
  return builder;
}

}  // namespace shk
