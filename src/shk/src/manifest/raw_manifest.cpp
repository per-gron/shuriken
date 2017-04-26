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

#include "raw_manifest.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "manifest/eval_env.h"
#include "manifest/lexer.h"
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

using ManifestPostprocessingData = std::vector<std::pair<
    const BindingEnv *, const Rule *>>;

struct ManifestParser {
  ManifestParser(
      Paths &paths,
      FileSystem &file_system,
      RawManifest &manifest,
      ManifestPostprocessingData &postprocessing_data,
      BindingEnv &env)
      : _paths(paths),
        _file_system(file_system),
        _manifest(manifest),
        _postprocessing_data(postprocessing_data),
        _env(env) {}

  /**
   * Load and parse a file.
   */
  void load(const std::string &filename, Lexer *parent) throw(ParseError) {
    std::string contents;

    try {
      contents = _file_system.readFile(filename);
    } catch (IoError &error) {
      auto err = "loading '" + filename + "': " + error.what();
      if (parent) {
        err = parent->error(err);
      }
      throw ParseError(err);
    }

    _manifest.manifest_files.push_back(filename);
    parse(filename, contents);
  }

  /**
   * Ninja is a bit inconsistent in when it evaluates variables in the manifest.
   * For inputs, outputs, implicit dependencies, order-only dependencies, the
   * pool name and bindings that are overridden in build statements, it
   * evaluates them eagerly at the point of parsing the build statement.
   * However, variable references in rules, for example command, description,
   * rspfile and depfile are not expanded until after the whole manifest is
   * parsed. This weirdness is what this method takes care of: It evaluates
   * the bindings that are supposed to be evaluated after the manifest has been
   * fully parsed.
   */
  void postprocessSteps() {
    assert(_postprocessing_data.size() == _manifest.steps.size());
    for (size_t i = 0; i < _postprocessing_data.size(); i++) {
      const BindingEnv *env;
      const Rule *rule;
      std::tie(env, rule) = _postprocessing_data[i];
      RawStep &step = _manifest.steps[i];

      const auto get_binding = [&](
          const std::string &key,
          StepEnv::EscapeKind escape_kind = StepEnv::EscapeKind::DO_NOT_ESCAPE) {
        return getBinding(
            *rule,
            *env,
            step.inputs,
            step.outputs,
            escape_kind,
            key);
      };
      const auto to_bool = [&](const std::string &value) {
        return !value.empty();
      };

      step.command = get_binding("command", StepEnv::EscapeKind::SHELL_ESCAPE);
      step.description = get_binding("description");
      step.generator = to_bool(get_binding("generator"));
      step.depfile = get_binding("depfile");
      step.rspfile = get_binding("rspfile");
      step.rspfile_content = get_binding("rspfile_content");
    }

    _manifest.build_dir = _env.lookupVariable("builddir");
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
        parseStep();
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
    Rule rule;
    rule.name = _lexer.readIdent("rule name");

    expectToken(_lexer, Lexer::NEWLINE);

    if (_env.lookupRuleCurrentScope(rule.name) != NULL) {
      _lexer.throwError("duplicate rule '" + rule.name + "'");
    }

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
      if (str.empty()) {
        _lexer.throwError("empty path");
      }
      const auto path = _paths.get(str);
      return path;
    } catch (PathError &error) {
      _lexer.throwError(error.what());
    }
  }

  std::vector<Path> evalStringsToPaths(
      const std::vector<EvalString> &outs,
      BindingEnv &env) throw(ParseError) {
    std::vector<Path> result;
    result.reserve(outs.size());
    for (auto i = outs.begin(); i != outs.end(); ++i) {
      result.push_back(toPath(i->evaluate(env)));
    }
    return result;
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
    const auto pool_name =
        StepEnvWithoutInAndOut(rule, env).lookupVariable("pool");

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

  void parseStep() throw(ParseError) {
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

    const auto implicit = _lexer.peekToken(Lexer::PIPE) ?
        parsePaths() :
        std::vector<EvalString>();

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

      // Evaluate the build statement's environment eagerly. This means that
      // variables set on the build statement only can see variables earlier
      // in the Ninja file. On the contrary, variables set on rules can see
      // variables that are set later on in the file. This behavior seems to be
      // incidental more than intentional but Shuriken intentionally copies
      // Ninja's behavior.
      env->addBinding(std::move(key), val.evaluate(_env));
      has_indent_token = _lexer.peekToken(Lexer::INDENT);
    }

    RawStep step;
    // Evaluate input and output paths eagerly. Paths that are set as inputs,
    // outputs, implicit or order-only dependencies can contain variables, but
    // these paths can only see variables that are set higher up in the Ninja
    // file. This is different from how variables on rules work, which can see
    // variables that are set both after the rule declaration and after the
    // build declaration. This behavior seems to be incidental more than
    // intentional but Shuriken intentionally copies Ninja's behavior.
    step.inputs = evalStringsToPaths(ins, *env);
    step.implicit_inputs = evalStringsToPaths(implicit, *env);
    step.dependencies = evalStringsToPaths(order_only, *env);
    step.outputs = evalStringsToPaths(outs, *env);

    step.pool_name = getPoolName(*rule, *env);

    _postprocessing_data.emplace_back(env, rule);
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

    // XXX This leaks memory (BindingEnv allocation). Is that ok?
    ManifestParser subparser(
        _paths,
        _file_system,
        _manifest,
        _postprocessing_data,
        new_scope ? *(new BindingEnv(_env)) : _env);

    subparser.load(path, &_lexer);

    expectToken(_lexer, Lexer::NEWLINE);
  }

  Paths &_paths;
  FileSystem &_file_system;
  RawManifest &_manifest;
  ManifestPostprocessingData &_postprocessing_data;
  BindingEnv &_env;
  Lexer _lexer;
};

}  // anonymous namespace

RawManifest parseManifest(
    Paths &paths,
    FileSystem &file_system,
    const std::string &path) throw(IoError, ParseError) {
  RawManifest manifest;
  manifest.pools["console"] = 1;

  BindingEnv env;
  Rule phony;
  phony.name = "phony";
  env.addRule(std::move(phony));

  ManifestPostprocessingData postprocessing_data;
  ManifestParser parser(paths, file_system, manifest, postprocessing_data, env);
  parser.load(path, nullptr);
  parser.postprocessSteps();
  return manifest;
}

}  // namespace shk
