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

#include <assert.h>

#include "fs/path.h"
#include "manifest/eval_env.h"
#include "util.h"

namespace shk {

std::string BindingEnv::lookupVariable(const std::string &var) {
  const auto i = _bindings.find(var);
  if (i != _bindings.end())
    return i->second;
  if (_parent)
    return _parent->lookupVariable(var);
  return "";
}

void BindingEnv::addBinding(std::string &&key, std::string &&val) {
  _bindings[std::move(key)] = std::move(val);
}

void BindingEnv::addRule(Rule &&rule) {
  assert(lookupRuleCurrentScope(rule.name) == NULL);
  _rules.emplace(std::string(rule.name), std::move(rule));
}

const Rule* BindingEnv::lookupRuleCurrentScope(
    const std::string &rule_name) const {
  const auto i = _rules.find(rule_name);
  if (i == _rules.end())
    return NULL;
  return &i->second;
}

const Rule* BindingEnv::lookupRule(const std::string &rule_name) const {
  const auto i = _rules.find(rule_name);
  if (i != _rules.end())
    return &i->second;
  if (_parent)
    return _parent->lookupRule(rule_name);
  return NULL;
}

const std::map<std::string, Rule>& BindingEnv::getRules() const {
  return _rules;
}

std::string BindingEnv::lookupWithFallback(
    const std::string &var,
    const EvalString *eval,
    Env &env) const {
  const auto i = _bindings.find(var);
  if (i != _bindings.end())
    return i->second;

  if (eval) {
    return eval->evaluate(env);
  }

  if (_parent) {
    return _parent->lookupVariable(var);
  }

  return "";
}

std::string StepEnvWithoutInAndOut::lookupVariable(const std::string &var) {
  if (_recursive) {
    std::vector<std::string>::const_iterator it;
    if ((it = find(_lookups.begin(), _lookups.end(), var)) != _lookups.end()) {
      std::string cycle;
      for (; it != _lookups.end(); ++it) {
        cycle.append(*it + " -> ");
      }
      cycle.append(var);
      fatal(("cycle in rule variables: " + cycle).c_str());
    }
  }

  // See notes on BindingEnv::lookupWithFallback.
  const EvalString *eval = _rule.getBinding(var);
  if (_recursive && eval) {
    _lookups.push_back(var);
  }

  // In practice, variables defined on rules never use another rule variable.
  // For performance, only start checking for cycles after the first lookup.
  _recursive = true;
  return _env.lookupWithFallback(var, eval, *this);
}

std::string StepEnv::lookupVariable(const std::string &var) {
  if (var == "in" || var == "in_newline") {
    return makePathList(_inputs, var == "in" ? ' ' : '\n');
  } else if (var == "out") {
    return makePathList(_outputs, ' ');
  }

  return StepEnvWithoutInAndOut::lookupVariable(var);
}

std::string StepEnv::makePathList(
    const std::vector<Path> &paths,
    char sep) {
  std::string result;
  for (const auto &path : paths) {
    if (!result.empty()) {
      result.push_back(sep);
    }
    const auto path_string = path.original();
    if (_escape_in_out == EscapeKind::SHELL_ESCAPE) {
#if _WIN32
      getWin32EscapedString(path_string, &result);
#else
      getShellEscapedString(path_string, &result);
#endif
    } else {
      result.append(path_string);
    }
  }
  return result;
}

}  // namespace shk
