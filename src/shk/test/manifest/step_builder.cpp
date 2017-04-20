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

Step StepBuilder::build(flatbuffers::FlatBufferBuilder &builder) {
  auto deps_vector = builder.CreateVector(
      _dependencies.data(), _dependencies.size());

  std::vector<flatbuffers::Offset<flatbuffers::String>> output_dirs;
  output_dirs.reserve(_output_dirs.size());
  for (const auto &output_dir : _output_dirs) {
    output_dirs.push_back(builder.CreateString(output_dir));
  }
  auto output_dirs_vector = builder.CreateVector(
      output_dirs.data(), output_dirs.size());

  auto pool_name_string = builder.CreateString(_pool_name);
  auto command_string = builder.CreateString(_command);
  auto description_string = builder.CreateString(_description);
  auto depfile_string = builder.CreateString(_depfile);
  auto rspfile_string = builder.CreateString(_rspfile);
  auto rspfile_content_string = builder.CreateString(_rspfile_content);

  std::vector<flatbuffers::Offset<flatbuffers::String>> generator_inputs;
  generator_inputs.reserve(_generator_inputs.size());
  for (const auto &generator_input : _generator_inputs) {
    generator_inputs.push_back(builder.CreateString(generator_input));
  }
  auto generator_inputs_vector = builder.CreateVector(
      generator_inputs.data(), generator_inputs.size());

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

  std::vector<std::string> output_dirs;
  output_dirs.reserve(step.outputDirs().size());
  for (const auto output_dir : step.outputDirs()) {
    output_dirs.emplace_back(output_dir);
  }
  builder.setOutputDirs(std::move(output_dirs));

  builder.setPoolName(std::string(step.poolName()));
  builder.setCommand(std::string(step.command()));
  builder.setDescription(std::string(step.description()));
  builder.setDepfile(std::string(step.depfile()));
  builder.setRspfile(std::string(step.rspfile()));
  builder.setRspfileContent(std::string(step.rspfileContent()));
  builder.setGenerator(step.generator());

  std::vector<std::string> generator_inputs;
  generator_inputs.reserve(step.generatorInputs().size());
  for (const auto generator_input : step.generatorInputs()) {
    generator_inputs.emplace_back(generator_input);
  }
  builder.setGeneratorInputs(generator_inputs);
  return builder;
}

}  // namespace shk
