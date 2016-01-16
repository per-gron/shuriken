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

#include "manifest.h"

#include <stdio.h>
#include <stdlib.h>

#include "eval_env.h"
#include "lexer.h"
#include "version.h"

namespace shk {
namespace {

/**
 * If the next token is not \a expected, throw a ParseError
 * saying "expectd foo, got bar".
 */
void expectToken(Lexer &lexer, Lexer::Token expected) throw(ParseError) {
  Lexer::Token token = lexer.readToken();
  if (token != expected) {
    std::string message = std::string("expected ") + Lexer::tokenName(expected);
    message += std::string(", got ") + Lexer::tokenName(token);
    message += Lexer::tokenErrorHint(expected);
    throw ParseError(lexer.error(message));
  }
}

}  // anonymous namespace

struct ManifestParser {
  ManifestParser(FileSystem &file_system, Manifest &manifest, BindingEnv &env)
      : _file_system(file_system),
        _manifest(manifest),
        _env(env) {}

  /**
   * Load and parse a file.
   */
  void load(const std::string &filename, Lexer *parent) throw(ParseError) {
    std::string contents;

    try {
      contents = _file_system.readFile(_file_system.paths().get(filename));
    } catch (IoError &error) {
      auto err = "loading '" + filename + "': " + error.what();
      if (parent) {
        err = parent->error(err);
      }
      throw ParseError(err);
    }

    parse(filename, contents);
  }

private:
  /**
   * Parse a file, given its contents as a string.
   */
  void parse(
      const std::string &filename,
      const std::string &input) throw(ParseError) {
    _lexer.start(filename, input);

    for (;;) {
      Lexer::Token token = _lexer.readToken();
      switch (token) {
      case Lexer::POOL:
        parsePool();
        break;
      case Lexer::BUILD:
        parseEdge();
        break;
      case Lexer::RULE:
        parseRule();
        break;
      case Lexer::DEFAULT:
        parseDefault();
        break;
      case Lexer::IDENT: {
        _lexer.unreadToken();
        std::string name;
        EvalString let_value;
        parseLet(&name, &let_value);
        std::string value = let_value.evaluate(_env);
        // Check ninja_required_version immediately so we can exit
        // before encountering any syntactic surprises.
        if (name == "ninja_required_version") {
          checkNinjaVersion(value);
        }
        _env.addBinding(std::move(name), std::move(value));
        break;
      }
      case Lexer::INCLUDE:
        parseFileInclude(false);
        break;
      case Lexer::SUBNINJA:
        parseFileInclude(true);
        break;
      case Lexer::ERROR: {
        _lexer.throwError(_lexer.describeLastError());
      }
      case Lexer::TEOF:
        return;
      case Lexer::NEWLINE:
        break;
      default:
        _lexer.throwError(std::string("unexpected ") + Lexer::tokenName(token));
      }
    }
  }

  void parsePool() throw(ParseError) {
    auto name = _lexer.readIdent("pool name");

    expectToken(_lexer, Lexer::NEWLINE);

    if (_manifest.pools.count(name) != 0) {
      _lexer.throwError("duplicate pool '" + name + "'");
    }

    int depth = -1;

    while (_lexer.peekToken(Lexer::INDENT)) {
      std::string key;
      EvalString value;
      parseLet(&key, &value);

      if (key == "depth") {
        std::string depth_string = value.evaluate(_env);
        depth = atol(depth_string.c_str());
        if (depth < 0) {
          _lexer.throwError("invalid pool depth");
        }
      } else {
        _lexer.throwError("unexpected variable '" + key + "'");
      }
    }

    if (depth < 0) {
      _lexer.throwError("expected 'depth =' line");
    }

    _manifest.pools.emplace(std::move(name), depth);
  }

  void parseRule() throw(ParseError) {
    const auto name = _lexer.readIdent("rule name");

    expectToken(_lexer, Lexer::NEWLINE);

    if (_env.lookupRuleCurrentScope(name) != NULL) {
      _lexer.throwError("duplicate rule '" + name + "'");
    }

    Rule rule;

    while (_lexer.peekToken(Lexer::INDENT)) {
      std::string key;
      EvalString value;
      parseLet(&key, &value);

      if (Rule::isReservedBinding(key)) {
        rule.bindings.emplace(std::move(key), std::move(value));
      } else {
        // Die on other keyvals for now; revisit if we want to add a
        // scope here.
        _lexer.throwError("unexpected variable '" + key + "'");
      }
    }

    if (rule.bindings["rspfile"].empty() !=
        rule.bindings["rspfile_content"].empty()) {
      _lexer.throwError(
          "rspfile and rspfile_content need to be both specified");
    }

    if (rule.bindings["command"].empty()) {
      _lexer.throwError("expected 'command =' line");
    }

    _env.addRule(std::move(rule));
  }

  void parseLet(
      std::string *key,
      EvalString *val) throw(ParseError) {
    *key = _lexer.readIdent("variable name");
    expectToken(_lexer, Lexer::EQUALS);
    _lexer.readVarValue(val);
  }

  Path toPath(const std::string &str) throw(ParseError) {
    try {
      return _file_system.paths().get(str);
    } catch (PathError &error) {
      _lexer.throwError(error.what());
    }
  }

  template<typename Out>
  void evalStringsToPaths(
      const std::vector<EvalString> &outs,
      BindingEnv &env,
      Out out) throw(ParseError) {
    for (auto i = outs.begin(); i != outs.end(); ++i) {
      *out++ = toPath(i->evaluate(env));
    }
  }

  /**
   * Parse zero or more input paths and add them to the provided vector.
   */
  std::vector<EvalString> parsePaths() throw(ParseError) {
    std::vector<EvalString> paths;
    for (;;) {
      EvalString in;
      _lexer.readPath(&in);
      if (in.empty()) {
        break;
      }
      paths.push_back(in);
    }
    return paths;
  }

  std::string getPoolName(const Rule &rule, const BindingEnv &env) {
    const auto pool_name = StepEnvWithoutInAndOut(rule, env).lookupVariable("pool");

    if (!pool_name.empty()) {
      if (_manifest.pools.count(pool_name) == 0) {
        _lexer.throwError("unknown pool name '" + pool_name + "'");
      }
    }

    return pool_name;
  }

  std::string getBinding(
      const Rule &rule,
      const BindingEnv &env,
      const std::vector<Path> &inputs,
      const std::vector<Path> &outputs,
      StepEnv::EscapeKind escape,
      const std::string &key) const {
    return StepEnv(rule, env, inputs, outputs, escape).lookupVariable(key);
  }

  void parseEdge() throw(ParseError) {
    const auto outs = parsePaths();
    if (outs.empty()) {
      _lexer.throwError("expected path");
    }

    expectToken(_lexer, Lexer::COLON);

    const auto rule_name = _lexer.readIdent("build command name");

    const Rule *rule = _env.lookupRule(rule_name);
    if (!rule) {
      _lexer.throwError("unknown build rule '" + rule_name + "'");
    }

    // XXX should we require one path here?
    const auto ins = parsePaths();

    // Add implicit deps
    const auto implicit = _lexer.peekToken(Lexer::PIPE) ?
        parsePaths() :
        std::vector<EvalString>();

    // Add all order-only deps, counting how many as we go.
    const auto order_only = _lexer.peekToken(Lexer::PIPE2) ?
        parsePaths() :
        std::vector<EvalString>();

    expectToken(_lexer, Lexer::NEWLINE);

    // Bindings on edges are rare, so allocate per-edge envs only when needed.
    bool has_indent_token = _lexer.peekToken(Lexer::INDENT);
    // XXX This leaks memory. Is that ok?
    BindingEnv * const env = has_indent_token ? new BindingEnv(_env) : &_env;
    while (has_indent_token) {
      std::string key;
      EvalString val;
      parseLet(&key, &val);

      env->addBinding(std::move(key), val.evaluate(_env));
      has_indent_token = _lexer.peekToken(Lexer::INDENT);
    }

    Step step;
    step.inputs.reserve(ins.size() + implicit.size());
    evalStringsToPaths(ins, *env, std::back_inserter(step.inputs));
    evalStringsToPaths(implicit, *env, std::back_inserter(step.inputs));

    step.dependencies.reserve(order_only.size());
    evalStringsToPaths(order_only, *env, std::back_inserter(step.inputs));

    step.outputs.reserve(outs.size());
    evalStringsToPaths(outs, *env, std::back_inserter(step.outputs));

    step.pool_name = getPoolName(*rule, _env);

    const auto get_binding = [&](const std::string &key) {
      return getBinding(
          *rule,
          *env,
          step.inputs,
          step.outputs,
          StepEnv::EscapeKind::DO_NOT_ESCAPE,
          key);
    };
    const auto to_bool = [&](const std::string &value) {
      return !value.empty();
    };

    step.command = get_binding("command");
    step.description = get_binding("description");
    step.restat = to_bool(get_binding("restat"));
    step.generator = to_bool(get_binding("generator"));
    step.depfile = toPath(get_binding("depfile"));
    step.rspfile = toPath(get_binding("rspfile"));
    step.rspfile_content = get_binding("rspfile_content");

    _manifest.steps.push_back(std::move(step));
  }

  void parseDefault() throw(ParseError) {
    EvalString eval;
    _lexer.readPath(&eval);
    if (eval.empty()) {
      _lexer.throwError("expected target name");
    }

    do {
      const auto path = eval.evaluate(_env);
      _manifest.defaults.push_back(toPath(path));

      eval.clear();
      _lexer.readPath(&eval);
    } while (!eval.empty());

    expectToken(_lexer, Lexer::NEWLINE);
  }

  /**
   * Parse either a 'subninja' or 'include' line.
   */
  void parseFileInclude(bool new_scope) throw(ParseError) {
    EvalString eval;
    _lexer.readPath(&eval);
    std::string path = eval.evaluate(_env);

    BindingEnv inner_env(_env);
    ManifestParser subparser(
        _file_system,
        _manifest,
        new_scope ? inner_env : _env);

    subparser.load(path, &_lexer);

    expectToken(_lexer, Lexer::NEWLINE);
  }

  FileSystem &_file_system;
  Manifest &_manifest;
  BindingEnv &_env;
  Lexer _lexer;
};

Manifest parseManifest(
    FileSystem &file_system,
    const std::string &path) throw(IoError, ParseError) {
  Manifest manifest;
  BindingEnv env;
  ManifestParser parser(file_system, manifest, env);
  parser.load(path, nullptr);
  return manifest;
}

}  // namespace shk
