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
  StepBuilder &setDepfile(std::string depfile);
  StepBuilder &setRspfile(std::string rspfile);
  StepBuilder &setRspfileContent(std::string rspfile_content);
  StepBuilder &setGenerator(bool generator);
  StepBuilder &setGeneratorInputs(std::vector<std::string> generator_inputs);
  StepBuilder &setGeneratorOutputs(std::vector<std::string> generator_outputs);

  Step build(flatbuffers::FlatBufferBuilder &builder);

  static StepBuilder fromStep(const Step &step);

 private:
  Hash _hash;
  std::vector<StepIndex> _dependencies;
  std::vector<std::string> _output_dirs;
  std::string _pool_name;
  std::string _command;
  std::string _description;
  std::string _depfile;
  std::string _rspfile;
  std::string _rspfile_content;
  bool _generator = false;
  std::vector<std::string> _generator_inputs;
  std::vector<std::string> _generator_outputs;
};

}  // namespace shk
