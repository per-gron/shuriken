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

std::string BindingEnv::lookupVariable(const std::string &var) {
  const auto i = _bindings.find(var);
  if (i != _bindings.end())
    return i->second;
  if (_parent)
    return _parent->lookupVariable(var);
  return "";
}

void BindingEnv::addBinding(const std::string &key, const std::string &val) {
  _bindings[key] = val;
}

void BindingEnv::addRule(const Rule *rule) {
  assert(lookupRuleCurrentScope(rule->name()) == NULL);
  _rules[rule->name()] = rule;
}

const Rule* BindingEnv::lookupRuleCurrentScope(
    const std::string &rule_name) const {
  const auto i = _rules.find(rule_name);
  if (i == _rules.end())
    return NULL;
  return i->second;
}

const Rule* BindingEnv::lookupRule(const std::string &rule_name) const {
  const auto i = _rules.find(rule_name);
  if (i != _rules.end())
    return i->second;
  if (_parent)
    return _parent->lookupRule(rule_name);
  return NULL;
}

void Rule::addBinding(const std::string &key, const EvalString &val) {
  _bindings[key] = val;
}

const EvalString *Rule::getBinding(const std::string &key) const {
  const auto i = _bindings.find(key);
  if (i == _bindings.end())
    return NULL;
  return &i->second;
}

// static
bool Rule::isReservedBinding(const std::string &var) {
  return var == "command" ||
      var == "depfile" ||
      var == "description" ||
      var == "deps" ||
      var == "generator" ||
      var == "pool" ||
      var == "restat" ||
      var == "rspfile" ||
      var == "rspfile_content" ||
      var == "msvc_deps_prefix";
}

const std::map<std::string, const Rule*>& BindingEnv::getRules() const {
  return _rules;
}

std::string BindingEnv::lookupWithFallback(
    const std::string &var,
    const EvalString *eval,
    Env *env) const {
  const auto i = _bindings.find(var);
  if (i != _bindings.end())
    return i->second;

  if (eval)
    return eval->evaluate(env);

  if (_parent)
    return _parent->lookupVariable(var);

  return "";
}

std::string EvalString::evaluate(Env *env) const {
  std::string result;
  for (auto i = _parsed.begin(); i != _parsed.end(); ++i) {
    if (i->second == RAW)
      result.append(i->first);
    else
      result.append(env->lookupVariable(i->first));
  }
  return result;
}

void EvalString::addText(StringPiece text) {
  // Add it to the end of an existing RAW token if possible.
  if (!_parsed.empty() && _parsed.back().second == RAW) {
    _parsed.back().first.append(text._str, text._len);
  } else {
    _parsed.push_back(make_pair(text.asString(), RAW));
  }
}
void EvalString::addSpecial(StringPiece text) {
  _parsed.push_back(make_pair(text.asString(), SPECIAL));
}

std::string EvalString::serialize() const {
  std::string result;
  for (auto i = _parsed.begin(); i != _parsed.end(); ++i) {
    result.append("[");
    if (i->second == SPECIAL)
      result.append("$");
    result.append(i->first);
    result.append("]");
  }
  return result;
}
