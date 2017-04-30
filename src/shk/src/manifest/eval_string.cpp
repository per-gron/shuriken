// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "manifest/eval_string.h"
#include "manifest/eval_env.h"

namespace shk {

std::string EvalString::evaluate(Env &env) const {
  std::string result;
  for (auto i = _parsed.begin(); i != _parsed.end(); ++i) {
    if (i->second == TokenType::RAW)
      result.append(i->first);
    else
      result.append(env.lookupVariable(i->first));
  }
  return result;
}

void EvalString::addText(string_view text) {
  // Add it to the end of an existing TokenType::RAW token if possible.
  if (!_parsed.empty() && _parsed.back().second == TokenType::RAW) {
    _parsed.back().first.append(text.data(), text.size());
  } else {
    _parsed.push_back(std::make_pair(
        std::string(text.data(), text.size()),
        TokenType::RAW));
  }
}
void EvalString::addSpecial(string_view text) {
  _parsed.push_back(std::make_pair(
      std::string(text.data(), text.size()),
      TokenType::SPECIAL));
}

std::string EvalString::serialize() const {
  std::string result;
  for (auto i = _parsed.begin(); i != _parsed.end(); ++i) {
    result.append("[");
    if (i->second == TokenType::SPECIAL)
      result.append("$");
    result.append(i->first);
    result.append("]");
  }
  return result;
}

}  // namespace shk
