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
  return Step(
      std::move(_hash),
      std::move(_dependencies),
      std::move(_output_dirs),
      std::move(_pool_name),
      std::move(_command),
      std::move(_description),
      std::move(_generator),
      std::move(_depfile),
      std::move(_rspfile),
      std::move(_rspfile_content));
}

Step::Step(
    Hash &&hash,
    std::vector<StepIndex> &&dependencies,
    std::vector<std::string> &&output_dirs,
    std::string &&pool_name,
    std::string &&command,
    std::string &&description,
    bool &&generator,
    std::string &&depfile,
    std::string &&rspfile,
    std::string &&rspfile_content)
    : _hash(std::move(hash)),
      _dependencies(std::move(dependencies)),
      _output_dirs(std::move(output_dirs)),
      _pool_name(std::move(pool_name)),
      _command(std::move(command)),
      _description(std::move(description)),
      _generator(std::move(generator)),
      _depfile(std::move(depfile)),
      _rspfile(std::move(rspfile)),
      _rspfile_content(std::move(rspfile_content)) {}

Step::Step() {}

Step::Builder Step::toBuilder() const {
  Builder builder;
  builder.setHash(Hash(_hash));
  builder.setDependencies(std::vector<StepIndex>(_dependencies));
  builder.setOutputDirs(std::vector<std::string>(_output_dirs));
  builder.setPoolName(std::string(_pool_name));
  builder.setCommand(std::string(_command));
  builder.setDescription(std::string(_description));
  builder.setGenerator(bool(_generator));
  builder.setDepfile(std::string(_depfile));
  builder.setRspfile(std::string(_rspfile));
  builder.setRspfileContent(std::string(_rspfile_content));
  return builder;
}

}  // namespace shk
