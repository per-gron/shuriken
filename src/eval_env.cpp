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

#include "eval_env.h"

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
  _bindings.emplace(std::move(key), std::move(val));
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

}  // namespace shk
