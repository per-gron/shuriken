#include "manifest/step.h"

#include <blake2.h>

namespace shk {

Step::Builder &Step::Builder::setHash(Hash &&hash) {
  _hash = std::move(hash);
  return *this;
}

Step::Builder &Step::Builder::setDependencies(
    std::vector<StepIndex> &&dependencies) {
  _dependencies = std::move(dependencies);
  return *this;
}

Step::Builder &Step::Builder::setOutputDirs(
    std::vector<std::string> &&output_dirs) {
  _output_dirs = std::move(output_dirs);
  return *this;
}

Step::Builder &Step::Builder::setPoolName(std::string &&pool_name) {
  _pool_name = std::move(pool_name);
  return *this;
}

Step::Builder &Step::Builder::setCommand(std::string &&command) {
  _command = std::move(command);
  return *this;
}

Step::Builder &Step::Builder::setDescription(std::string &&description) {
  _description = std::move(description);
  return *this;
}

Step::Builder &Step::Builder::setGenerator(bool &&generator) {
  _generator = std::move(generator);
  return *this;
}

Step::Builder &Step::Builder::setDepfile(std::string &&depfile) {
  _depfile = std::move(depfile);
  return *this;
}

Step::Builder &Step::Builder::setRspfile(std::string &&rspfile) {
  _rspfile = std::move(rspfile);
  return *this;
}

Step::Builder &Step::Builder::setRspfileContent(std::string &&rspfile_content) {
  _rspfile_content = std::move(rspfile_content);
  return *this;
}


Step Step::Builder::build() {
  auto builder = std::make_shared<flatbuffers::FlatBufferBuilder>(1024);

  auto deps_vector = builder->CreateVector(
      _dependencies.data(), _dependencies.size());

  std::vector<flatbuffers::Offset<flatbuffers::String>> output_dirs;
  output_dirs.reserve(_output_dirs.size());
  for (const auto &output_dir : _output_dirs) {
    output_dirs.push_back(builder->CreateString(output_dir));
  }
  auto output_dirs_vector = builder->CreateVector(
      output_dirs.data(), output_dirs.size());

  auto pool_name_string = builder->CreateString(_pool_name);
  auto command_string = builder->CreateString(_command);
  auto description_string = builder->CreateString(_description);
  auto depfile_string = builder->CreateString(_depfile);
  auto rspfile_string = builder->CreateString(_rspfile);
  auto rspfile_content_string = builder->CreateString(_rspfile_content);

  ShkManifest::StepBuilder step_builder(*builder);
  step_builder.add_hash(
      reinterpret_cast<const ShkManifest::Hash *>(_hash.data.data()));
  step_builder.add_dependencies(deps_vector);
  step_builder.add_output_dirs(output_dirs_vector);
  step_builder.add_pool_name(pool_name_string);
  step_builder.add_command(command_string);
  step_builder.add_description(description_string);
  step_builder.add_generator(_generator);
  step_builder.add_depfile(depfile_string);
  step_builder.add_rspfile(rspfile_string);
  step_builder.add_rspfile_content(rspfile_content_string);
  builder->Finish(step_builder.Finish());

  return Step(std::move(builder));
}

Step::Step(std::shared_ptr<flatbuffers::FlatBufferBuilder> &&data)
    : _data(std::move(data)),
      _step(flatbuffers::GetRoot<ShkManifest::Step>(
          _data->GetBufferPointer())) {}

Step::Builder Step::toBuilder() const {
  Builder builder;
  builder.setHash(Hash(hash()));
  builder.setDependencies(std::vector<StepIndex>(
      dependencies().data(),
      dependencies().data() + dependencies().size()));

  std::vector<std::string> output_dirs;
  output_dirs.reserve(outputDirs().size());
  for (const auto output_dir : outputDirs()) {
    output_dirs.emplace_back(output_dir);
  }
  builder.setOutputDirs(std::move(output_dirs));

  builder.setPoolName(std::string(poolName()));
  builder.setCommand(std::string(command()));
  builder.setDescription(std::string(description()));
  builder.setGenerator(bool(generator()));
  builder.setDepfile(std::string(depfile()));
  builder.setRspfile(std::string(rspfile()));
  builder.setRspfileContent(std::string(rspfileContent()));
  return builder;
}

}  // namespace shk
