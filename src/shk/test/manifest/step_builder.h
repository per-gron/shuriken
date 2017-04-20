#pragma once

#include "manifest/step.h"

namespace shk {

class StepBuilder {
 public:
  StepBuilder &setHash(Hash hash);
  StepBuilder &setDependencies(std::vector<StepIndex> dependencies);
  StepBuilder &setOutputDirs(std::vector<std::string> output_dirs);
  StepBuilder &setPoolName(std::string pool_name);
  StepBuilder &setCommand(std::string command);
  StepBuilder &setDescription(std::string description);
  StepBuilder &setGenerator(bool generator);
  StepBuilder &setDepfile(std::string depfile);
  StepBuilder &setRspfile(std::string rspfile);
  StepBuilder &setRspfileContent(std::string rspfile_content);

  Step build(flatbuffers::FlatBufferBuilder &builder);

  static StepBuilder fromStep(const Step &step);

 private:
  Hash _hash;
  std::vector<StepIndex> _dependencies;
  std::vector<std::string> _output_dirs;
  std::string _pool_name;
  std::string _command;
  std::string _description;
  bool _generator = false;
  std::string _depfile;
  std::string _rspfile;
  std::string _rspfile_content;
};

}  // namespace shk
