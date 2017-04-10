#include "manifest/step.h"

#include <blake2.h>

namespace shk {

Step::Builder &Step::Builder::setInputs(std::vector<Path> &&inputs) {
  _inputs = std::move(inputs);
  return *this;
}

Step::Builder &Step::Builder::setImplicitInputs(std::vector<Path> &&implicit_inputs) {
  _implicit_inputs = std::move(implicit_inputs);
  return *this;
}

Step::Builder &Step::Builder::setDependencies(std::vector<Path> &&dependencies) {
  _dependencies = std::move(dependencies);
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

Step::Builder &Step::Builder::setDepfile(Optional<Path> &&depfile) {
  _depfile = std::move(depfile);
  return *this;
}

Step::Builder &Step::Builder::setRspfile(Optional<Path> &&rspfile) {
  _rspfile = std::move(rspfile);
  return *this;
}

Step::Builder &Step::Builder::setRspfileContent(std::string &&rspfile_content) {
  _rspfile_content = std::move(rspfile_content);
  return *this;
}


Step Step::Builder::build() {
  return Step(
      std::move(_inputs),
      std::move(_implicit_inputs),
      std::move(_dependencies),
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
    std::vector<Path> &&inputs,
    std::vector<Path> &&implicit_inputs,
    std::vector<Path> &&dependencies,
    std::vector<Path> &&outputs,
    std::string &&pool_name,
    std::string &&command,
    std::string &&description,
    bool &&generator,
    Optional<Path> &&depfile,
    Optional<Path> &&rspfile,
    std::string &&rspfile_content)
    : inputs(std::move(inputs)),
      implicit_inputs(std::move(implicit_inputs)),
      dependencies(std::move(dependencies)),
      outputs(std::move(outputs)),
      pool_name(std::move(pool_name)),
      command(std::move(command)),
      description(std::move(description)),
      generator(std::move(generator)),
      depfile(std::move(depfile)),
      rspfile(std::move(rspfile)),
      rspfile_content(std::move(rspfile_content)) {}

Step::Step() {}

Step::Step(RawStep &&raw_step)
    : inputs(std::move(raw_step.inputs)),
      implicit_inputs(std::move(raw_step.implicit_inputs)),
      dependencies(std::move(raw_step.dependencies)),
      outputs(std::move(raw_step.outputs)),
      pool_name(std::move(raw_step.pool_name)),
      command(std::move(raw_step.command)),
      description(std::move(raw_step.description)),
      generator(raw_step.generator),
      depfile(raw_step.depfile),
      rspfile(raw_step.rspfile),
      rspfile_content(std::move(raw_step.rspfile_content)) {}

Step::Builder Step::toBuilder() const {
  Builder builder;
  builder.setInputs(std::vector<Path>(inputs));
  builder.setImplicitInputs(std::vector<Path>(implicit_inputs));
  builder.setDependencies(std::vector<Path>(dependencies));
  builder.setOutputs(std::vector<Path>(outputs));
  builder.setPoolName(std::string(pool_name));
  builder.setCommand(std::string(command));
  builder.setDescription(std::string(description));
  builder.setGenerator(bool(generator));
  builder.setDepfile(Optional<Path>(depfile));
  builder.setRspfile(Optional<Path>(rspfile));
  builder.setRspfileContent(std::string(rspfile_content));
  return builder;
}

Hash Step::hash() const {
  Hash hash;
  blake2b_state state;
  blake2b_init(&state, hash.data.size());
  const auto hash_string = [&](const std::string &string) {
    blake2b_update(
        &state,
        reinterpret_cast<const uint8_t *>(string.data()),
        string.size() + 1);  // Include trailing \0
  };

  const auto hash_paths = [&](const std::vector<Path> &paths) {
    for (const auto &path : paths) {
      hash_string(path.original());
    }
    // Add a separator, so that it is impossible to get the same hash by just
    // removing a path from the end of one list and adding it to the beginning
    // of the next.
    hash_string("");  // "" is not a valid path so it is a good separator
  };
  hash_paths(inputs);
  hash_paths(implicit_inputs);
  hash_paths(dependencies);
  hash_paths(outputs);
  hash_string(generator ? "" : command);
  hash_string(rspfile_content);

  blake2b_final(&state, hash.data.data(), hash.data.size());
  return hash;
}

}  // namespace shk
