#include "manifest/step.h"

#include <blake2.h>

namespace shk {

Step::Builder &Step::Builder::setHash(Hash &&hash) {
  _hash = std::move(hash);
  return *this;
}

Step::Builder &Step::Builder::setDependencies(
    std::vector<Path> &&dependencies) {
  _dependencies = std::move(dependencies);
  return *this;
}

Step::Builder &Step::Builder::setOutputDirs(
    std::vector<std::string> &&output_dirs) {
  _output_dirs = std::move(output_dirs);
  return *this;
}

Step::Builder &Step::Builder::setOutputs(std::vector<Path> &&outputs) {
  _outputs = std::move(outputs);
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
      std::move(_outputs),
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
    std::vector<Path> &&dependencies,
    std::vector<std::string> &&output_dirs,
    std::vector<Path> &&outputs,
    std::string &&pool_name,
    std::string &&command,
    std::string &&description,
    bool &&generator,
    std::string &&depfile,
    std::string &&rspfile,
    std::string &&rspfile_content)
    : hash(std::move(hash)),
      dependencies(std::move(dependencies)),
      output_dirs(std::move(output_dirs)),
      outputs(std::move(outputs)),
      pool_name(std::move(pool_name)),
      command(std::move(command)),
      description(std::move(description)),
      generator(std::move(generator)),
      depfile(std::move(depfile)),
      rspfile(std::move(rspfile)),
      rspfile_content(std::move(rspfile_content)) {}

Step::Step() {}

Step::Builder Step::toBuilder() const {
  Builder builder;
  builder.setHash(Hash(hash));
  builder.setDependencies(std::vector<Path>(dependencies));
  builder.setOutputDirs(std::vector<std::string>(output_dirs));
  builder.setOutputs(std::vector<Path>(outputs));
  builder.setPoolName(std::string(pool_name));
  builder.setCommand(std::string(command));
  builder.setDescription(std::string(description));
  builder.setGenerator(bool(generator));
  builder.setDepfile(std::string(depfile));
  builder.setRspfile(std::string(rspfile));
  builder.setRspfileContent(std::string(rspfile_content));
  return builder;
}

}  // namespace shk
