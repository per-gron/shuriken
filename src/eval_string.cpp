// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "eval_string.h"
#include "eval_env.h"

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

void EvalString::addText(StringPiece text) {
  // Add it to the end of an existing TokenType::RAW token if possible.
  if (!_parsed.empty() && _parsed.back().second == TokenType::RAW) {
    _parsed.back().first.append(text._str, text._len);
  } else {
    _parsed.push_back(make_pair(text.asString(), TokenType::RAW));
  }
}
void EvalString::addSpecial(StringPiece text) {
  _parsed.push_back(make_pair(text.asString(), TokenType::SPECIAL));
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
