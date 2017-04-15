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

#pragma once

#include <map>
#include <string>
#include <vector>

#include "manifest/eval_string.h"
#include "manifest/rule.h"

namespace shk {

class Path;

/**
 * An interface for a scope for variable (e.g. "$foo") lookups.
 *
 * Used in the manifest parser.
 */
class Env {
 public:
  virtual ~Env() {}
  virtual std::string lookupVariable(const std::string& var) = 0;
};

/**
 * An Env which contains a mapping of variables to values
 * as well as a pointer to a parent scope.
 *
 * BindingEnvs are created and manipulated by the manifest parser only. After
 * parsing is complete, BindingEnvs are all const and should not be modified.
 * This is important for thread safety (and sanity in general).
 */
struct BindingEnv : public Env {
  BindingEnv() : _parent(NULL) {}
  explicit BindingEnv(BindingEnv &parent) : _parent(&parent) {}

  std::string lookupVariable(const std::string& var) override;

  void addRule(Rule &&rule);
  const Rule *lookupRule(const std::string& rule_name) const;
  const Rule *lookupRuleCurrentScope(const std::string& rule_name) const;
  const std::map<std::string, Rule> &getRules() const;

  void addBinding(std::string &&key, std::string &&val);

  /**
   * This is tricky.  Edges want lookup scope to go in this order:
   * 1) value set on edge itself
   * 2) value set on rule, with expansion in the edge's scope
   * 3) value set on enclosing scope of edge
   * This function takes as parameters the necessary info to do (2).
   */
  std::string lookupWithFallback(
      const std::string &var,
      const EvalString *eval,
      Env &env) const;

private:
  std::map<std::string, std::string> _bindings;
  std::map<std::string, Rule> _rules;
  BindingEnv * const _parent;
};

/**
 * An Env for a build step, NOT providing $in and $out. This is used when
 * looking up the pool binding, which is done before there are inputs and
 * outputs. (The class is also used when doing lookups that do have $in and
 * $out, see StepEnv.)
 *
 * StepEnvWithoutInAndOut is a one-shot object. It supports only one lookup.
 * After that it is consumed and another one has to be created.
 */
struct StepEnvWithoutInAndOut : public Env {
  StepEnvWithoutInAndOut(const Rule &rule, const BindingEnv &env)
      : _rule(rule),
        _env(env),
        _recursive(false) {}

  std::string lookupVariable(const std::string &var) override;

 private:
  std::vector<std::string> _lookups;
  const Rule &_rule;
  const BindingEnv &_env;
  bool _recursive;
};

/**
 * An Env for a build step, providing $in and $out.
 *
 * StepEnv is a one-shot object. It supports only one lookup. After that it is
 * consumed and another one has to be created.
 */
struct StepEnv : public StepEnvWithoutInAndOut {
  enum class EscapeKind { SHELL_ESCAPE, DO_NOT_ESCAPE };

  StepEnv(
      const Rule &rule,
      const BindingEnv &env,
      const std::vector<Path> &inputs,
      const std::vector<Path> &outputs,
      EscapeKind escape)
      : StepEnvWithoutInAndOut(rule, env),
        _inputs(inputs),
        _outputs(outputs),
        _escape_in_out(escape) {}

  std::string lookupVariable(const std::string& var) override;

 private:
  /**
   * Given some Paths, construct a list of paths suitable for a command line.
   */
  std::string makePathList(
      const std::vector<Path> &paths,
      char sep);

  const std::vector<Path> &_inputs;
  const std::vector<Path> &_outputs;
  const EscapeKind _escape_in_out;
};

}  // namespace shk
