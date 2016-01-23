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

#include <unordered_map>
#include <vector>

#include <catch.hpp>

#include "manifest.h"

#include "in_memory_file_system.h"

namespace shk {
namespace {

void verifyManifest(const Manifest &manifest) {
  for (const auto &step : manifest.steps) {
    // All edges need at least one output.
    CHECK(!step.outputs.empty());
  }
}

Manifest parse(Paths &paths, FileSystem &file_system, const char* input) {
  writeFile(file_system, "build.ninja", input);
  const auto manifest = parseManifest(paths, file_system, "build.ninja");
  verifyManifest(manifest);
  return manifest;
}

std::string parseError(Paths &paths, FileSystem &file_system, const char* input) {
  try {
    parse(paths, file_system, input);
    CHECK(!"parse should have failed");
    return "";
  } catch (ParseError &error) {
    return error.what();
  }
}

Step parseStep(Paths &paths, FileSystem &file_system, const char* input) {
  const auto manifest = parse(paths, file_system, input);
  REQUIRE(manifest.steps.size() == 1);
  return manifest.steps[0];
}

}  // anonymous namespace

TEST_CASE("Manifest") {
  InMemoryFileSystem fs;
  Paths paths(fs);

  SECTION("Empty") {
    parse(paths, fs, "");
  }

  SECTION("Rules") {
    const auto step = parseStep(paths, fs,
        "rule cat\n"
        "  command = cat $in > $out\n"
        "\n"
        "rule date\n"
        "  command = date > $out\n"
        "\n"
        "build result: cat in_1.cc in-2.O\n");

    CHECK(step.command == "cat in_1.cc in-2.O > result");
    CHECK(!step.generator);
    CHECK(!step.restat);
  }

  SECTION("DotPath") {
    const auto step = parseStep(paths, fs,
        "rule cat\n"
        "  command = cat $in > $out\n"
        "\n"
        "rule date\n"
        "  command = date > $out\n"
        "\n"
        "build result: cat . in-2.O\n");

    CHECK(step.command == "cat . in-2.O > result");
    CHECK(!step.generator);
    CHECK(!step.restat);
  }

  SECTION("RuleAttributes") {
    // Check that all of the allowed rule attributes are parsed ok.
    const auto step = parseStep(paths, fs,
        "rule cat\n"
        "  command = a\n"
        "  depfile = b\n"
        "  deps = c\n"
        "  description = d\n"
        "  generator = e\n"
        "  restat = f\n"
        "  rspfile = g\n"
        "  rspfile_content = h\n"
        "\n"
        "build result: cat\n");

    CHECK(step.command == "a");
    CHECK(step.depfile.original() == "b");
    CHECK(step.description == "d");
    CHECK(step.generator);
    CHECK(step.restat);
    CHECK(step.rspfile.original() == "g");
    CHECK(step.rspfile_content == "h");
  }

  SECTION("IgnoreIndentedComments") {
    const auto step = parseStep(paths, fs,
        "  #indented comment\n"
        "rule cat\n"
        "  command = cat $in > $out\n"
        "  #generator = 1\n"
        "  restat = 1 # comment\n"
        "  #comment\n"
        "build result: cat in_1.cc in-2.O\n"
        "  #comment\n");

    CHECK(step.command == "cat in_1.cc in-2.O > result");
    CHECK(!step.generator);
    CHECK(step.restat);
  }

  SECTION("ResponseFiles") {
    const auto step = parseStep(paths, fs,
        "rule cat_rsp\n"
        "  command = cat $rspfile > $out\n"
        "  rspfile = $rspfile\n"
        "  rspfile_content = $in\n"
        "\n"
        "build out: cat_rsp in\n"
        "  rspfile=out.rsp\n");

    CHECK(step.rspfile.original() == "out.rsp");
    CHECK(step.rspfile_content == "in");
  }

  SECTION("InNewline") {
    const auto step = parseStep(paths, fs,
        "rule cat_rsp\n"
        "  command = cat $in_newline > $out\n"
        "\n"
        "build out: cat_rsp in in2\n"
        "  rspfile=out.rsp\n");

    CHECK(step.command == "cat in\nin2 > out");
  }

  SECTION("Variables") {
    const auto manifest = parse(paths, fs,
        "l = one-letter-test\n"
        "rule link\n"
        "  command = ld $l $extra $with_under -o $out $in\n"
        "\n"
        "extra = -pthread\n"
        "with_under = -under\n"
        "build a: link b c\n"
        "nested1 = 1\n"
        "nested2 = $nested1/2\n"
        "build supernested: link x\n"
        "  extra = $nested2/3\n");

    REQUIRE(manifest.steps.size() == 2);

    const auto &a = manifest.steps[0];
    CHECK(a.command == "ld one-letter-test -pthread -under -o a b c");

    const auto &supernested = manifest.steps[1];
    CHECK(supernested.command == "ld one-letter-test 1/2/3 -under -o supernested x");
  }

  SECTION("VariableScope") {
    const auto manifest = parse(paths, fs,
        "foo = bar\n"
        "rule cmd\n"
        "  command = cmd $foo $in $out\n"
        "\n"
        "build inner: cmd a\n"
        "  foo = baz\n"
        "build outer: cmd b\n"
        "\n");  // Extra newline after build line tickles a regression.

    REQUIRE(manifest.steps.size() == 2);
    CHECK(manifest.steps[0].command == "cmd baz a inner");
    CHECK(manifest.steps[1].command == "cmd bar b outer");
  }

  SECTION("Continuation") {
    const auto step = parseStep(paths, fs,
        "rule link\n"
        "  command = foo bar $\n"
        "    baz\n"
        "\n"
        "build a: link c $\n"
        " d e f\n");

    CHECK(step.command == "foo bar baz");
  }

  SECTION("Backslash") {
    const auto step = parseStep(paths, fs,
        "foo = bar\\baz\n"
        "foo2 = bar\\ baz\n"
        "\n"
        "rule r\n"
        "  command = '$foo'$foo2'\n"
        "\n"
        "build a: r\n");
    CHECK(step.command == "'bar\\baz'bar\\ baz'");
  }

  SECTION("Comment") {
    const auto step = parseStep(paths, fs,
        "# this is a comment\n"
        "foo = not # a comment\n"
        "\n"
        "rule r\n"
        "  command = $foo\n"
        "\n"
        "build a: r\n");
    CHECK(step.command == "not # a comment");
  }

  SECTION("Dollars") {
    const auto step = parseStep(paths, fs,
        "rule foo\n"
        "  command = ${out}bar$$baz$$$\n"
        "blah\n"
        "  description = $x\n"
        "x = $$dollar\n"
        "build $x: foo y\n");
    CHECK(step.description == "$dollar");
  #ifdef _WIN32
    CHECK(step.command == "$dollarbar$baz$blah");
  #else
    CHECK(step.command == "'$dollar'bar$baz$blah");
  #endif
  }

  SECTION("EscapeSpaces") {
    const auto step = parseStep(paths, fs,
        "rule spaces\n"
        "  command = something\n"
        "build foo$ bar: spaces $$one two$$$ three\n");
    REQUIRE(step.outputs.size() == 1);
    CHECK(step.outputs[0].original() == "foo bar");
    REQUIRE(step.inputs.size() == 2);
    CHECK(step.inputs[0].original() == "$one");
    CHECK(step.inputs[1].original() == "two$ three");
    CHECK(step.command == "something");
  }

  SECTION("CanonicalizeFile") {
    const auto manifest = parse(paths, fs,
        "rule cat\n"
        "  command = cat $in > $out\n"
        "build out/1: cat in/1\n"
        "build out/2: cat in//1\n");

    REQUIRE(manifest.steps.size() == 2);
    const auto &step_0 = manifest.steps[0];
    const auto &step_1 = manifest.steps[1];
    REQUIRE(step_0.inputs.size() == 1);
    REQUIRE(step_1.inputs.size() == 1);
    CHECK(step_0.inputs[0].isSame(step_1.inputs[0]));
  }

  SECTION("PathVariables") {
    const auto step = parseStep(paths, fs,
        "rule cat\n"
        "  command = cat $in > $out\n"
        "dir = out\n"
        "build $dir/exe: cat src\n");

    CHECK(step.command == "cat src > out/exe");
  }

  SECTION("ReservedWords") {
    const auto manifest = parse(paths, fs,
        "rule build\n"
        "  command = rule run $out $in\n"
        "build subninja: build include default foo.cc\n"
        "default subninja\n");

    REQUIRE(manifest.defaults.size() == 1);
    CHECK(manifest.defaults[0].original() == "subninja");

    REQUIRE(manifest.steps.size() == 1);
    const auto &step = manifest.steps[0];
    CHECK(step.command == "rule run subninja include default foo.cc");
  }

  SECTION("Errors") {
    CHECK(parseError(paths, fs, "subn") ==
        "build.ninja:1: expected '=', got eof\n"
        "subn\n"
        "    ^ near here");

    CHECK(parseError(paths, fs, "foobar") ==
        "build.ninja:1: expected '=', got eof\n"
        "foobar\n"
        "      ^ near here");

    CHECK(parseError(paths, fs, "x 3") ==
        "build.ninja:1: expected '=', got identifier\n"
        "x 3\n"
        "  ^ near here");

    CHECK(parseError(paths, fs, "x = 3") ==
        "build.ninja:1: unexpected EOF\n"
        "x = 3\n"
        "     ^ near here");

    CHECK(parseError(paths, fs, "x = 3\ny 2") ==
        "build.ninja:2: expected '=', got identifier\n"
        "y 2\n"
        "  ^ near here");

    CHECK(parseError(paths, fs, "x = $") ==
        "build.ninja:1: bad $-escape (literal $ must be written as $$)\n"
        "x = $\n"
        "    ^ near here");

    CHECK(parseError(paths, fs, "x = $\n $[\n") ==
        "build.ninja:2: bad $-escape (literal $ must be written as $$)\n"
        " $[\n"
        " ^ near here");

    CHECK(parseError(paths, fs, "x = a$\n b$\n $\n") ==
        "build.ninja:4: unexpected EOF\n");

    CHECK(parseError(paths, fs, "build\n") ==
        "build.ninja:1: expected path\n"
        "build\n"
        "     ^ near here");

    CHECK(parseError(paths, fs, "build x: y z\n") ==
        "build.ninja:1: unknown build rule 'y'\n"
        "build x: y z\n"
        "       ^ near here");

    CHECK(parseError(paths, fs, "build x:: y z\n") ==
        "build.ninja:1: expected build command name\n"
        "build x:: y z\n"
        "       ^ near here");

    CHECK(
        parseError(paths, fs,
            "rule cat\n  command = cat ok\n"
            "build x: cat $\n :\n") ==
        "build.ninja:4: expected newline, got ':'\n"
        " :\n"
        " ^ near here");

    CHECK(parseError(paths, fs, "rule cat\n") ==
        "build.ninja:2: expected 'command =' line\n");

    CHECK(
        parseError(paths, fs,
            "rule cat\n"
            "  command = echo\n"
            "rule cat\n"
            "  command = echo\n") ==
        "build.ninja:3: duplicate rule 'cat'\n"
        "rule cat\n"
        "        ^ near here");

    CHECK(
        parseError(paths, fs,
            "rule cat\n"
            "  command = echo\n"
            "  rspfile = cat.rsp\n") ==
        "build.ninja:4: rspfile and rspfile_content need to be both specified\n");

    CHECK(
        parseError(paths, fs,
            "rule cat\n"
            "  command = ${fafsd\n"
            "foo = bar\n") ==
        "build.ninja:2: bad $-escape (literal $ must be written as $$)\n"
        "  command = ${fafsd\n"
        "            ^ near here");

    CHECK(
        parseError(paths, fs,
            "rule cat\n"
            "  command = cat\n"
            "build $.: cat foo\n") ==
        "build.ninja:3: bad $-escape (literal $ must be written as $$)\n"
        "build $.: cat foo\n"
        "      ^ near here");

    CHECK(
        parseError(paths, fs,
            "rule cat\n"
            "  command = cat\n"
            "build $: cat foo\n") ==
        "build.ninja:3: expected ':', got newline ($ also escapes ':')\n"
        "build $: cat foo\n"
        "                ^ near here");

    CHECK(parseError(paths, fs, "rule %foo\n") ==
        "build.ninja:1: expected rule name\n");

    CHECK(
        parseError(paths, fs,
            "rule cc\n"
            "  command = foo\n"
            "  othervar = bar\n") ==
        "build.ninja:3: unexpected variable 'othervar'\n"
        "  othervar = bar\n"
        "                ^ near here");

    CHECK(
        parseError(paths, fs,
            "rule cc\n  command = foo\n"
            "build $.: cc bar.cc\n") ==
        "build.ninja:3: bad $-escape (literal $ must be written as $$)\n"
        "build $.: cc bar.cc\n"
        "      ^ near here");

    CHECK(
        parseError(paths, fs, "rule cc\n  command = foo\n  && bar") ==
        "build.ninja:3: expected variable name\n");

    CHECK(
        parseError(paths, fs,
            "rule cc\n  command = foo\n"
            "build $: cc bar.cc\n") ==
        "build.ninja:3: expected ':', got newline ($ also escapes ':')\n"
        "build $: cc bar.cc\n"
        "                  ^ near here");

    CHECK(parseError(paths, fs, "default\n") ==
        "build.ninja:1: expected target name\n"
        "default\n"
        "       ^ near here");

    CHECK(
        parseError(paths, fs,
            "rule r\n  command = r\n"
            "build b: r\n"
            "default b:\n") ==
        "build.ninja:4: expected newline, got ':'\n"
        "default b:\n"
        "         ^ near here");

    CHECK(parseError(paths, fs, "default $a\n") ==
        "build.ninja:1: empty path\n"
        "default $a\n"
        "          ^ near here");

    // XXX the line number is wrong; we should evaluate paths in ParseEdge
    // as we see them, not after we've read them all!
    CHECK(
        parseError(paths, fs,
            "rule r\n"
            "  command = r\n"
            "build $a: r $c\n") ==
        "build.ninja:4: empty path\n");

    // the indented blank line must terminate the rule
    // this also verifies that "unexpected (token)" errors are correct
    CHECK(
        parseError(paths, fs,
            "rule r\n"
            "  command = r\n"
            "  \n"
            "  generator = 1\n") ==
        "build.ninja:4: unexpected indent\n");

    CHECK(parseError(paths, fs, "pool\n") ==
        "build.ninja:1: expected pool name\n");

    CHECK(parseError(paths, fs, "pool foo\n") ==
        "build.ninja:2: expected 'depth =' line\n");

    CHECK(
        parseError(paths, fs,
            "pool foo\n"
            "  depth = 4\n"
            "pool foo\n") ==
        "build.ninja:3: duplicate pool 'foo'\n"
        "pool foo\n"
        "        ^ near here");

    CHECK(
        parseError(paths, fs,
            "pool foo\n"
            "  depth = -1\n") ==
        "build.ninja:2: invalid pool depth\n"
        "  depth = -1\n"
        "            ^ near here");

    CHECK(
        parseError(paths, fs,
            "pool foo\n"
            "  bar = 1\n") ==
        "build.ninja:2: unexpected variable 'bar'\n"
        "  bar = 1\n"
        "         ^ near here");

    // Pool names are dereferenced at edge parsing time.
    CHECK(
        parseError(paths, fs,
            "rule run\n"
            "  command = echo\n"
            "  pool = unnamed_pool\n"
            "build out: run in\n") ==
        "build.ninja:5: unknown pool name 'unnamed_pool'\n");
  }

  SECTION("MissingInput") {
    try {
      parseManifest(paths, fs, "build.ninja");
      CHECK(!"parse should have failed");
    } catch (ParseError &error) {
      CHECK("loading 'build.ninja': No such file or directory" ==
          std::string(error.what()));
    }
  }

  SECTION("MultipleOutputs") {
    const auto step = parseStep(paths, fs,
        "rule cc\n"
        "  command = foo\n"
        "  depfile = bar\n"
        "build a.o b.o: cc c.cc\n");
    REQUIRE(step.outputs.size() == 2);
    CHECK(step.outputs[0].original() == "a.o");
    CHECK(step.outputs[1].original() == "b.o");
  }

  SECTION("SubNinja") {
    writeFile(fs, "test.ninja",
        "var = inner\n"
        "build $builddir/inner: varref\n");
    const auto manifest = parse(paths, fs,
        "builddir = some_dir\n"
        "rule varref\n"
        "  command = varref $var\n"
        "var = outer\n"
        "build $builddir/outer: varref\n"
        "subninja test.ninja\n"
        "build $builddir/outer2: varref\n");

    REQUIRE(manifest.steps.size() == 3);
    CHECK(manifest.steps[0].outputs[0].original() == "some_dir/outer");
    // Verify our builddir setting is inherited.
    CHECK(manifest.steps[1].outputs[0].original() == "some_dir/inner");
    CHECK(manifest.steps[2].outputs[0].original() == "some_dir/outer2");

    CHECK(manifest.steps[0].command == "varref outer");
    CHECK(manifest.steps[1].command == "varref inner");
    CHECK(manifest.steps[2].command == "varref outer");
  }

  SECTION("MissingSubNinja") {
    CHECK(parseError(paths, fs, "subninja foo.ninja\n") ==
        "build.ninja:1: loading 'foo.ninja': No such file or directory\n"
        "subninja foo.ninja\n"
        "                  ^ near here");
  }

  SECTION("DuplicateRuleInDifferentSubninjas") {
    // Test that rules are scoped to subninjas.
    writeFile(fs, "test.ninja",
        "rule cat\n"
        "  command = cat\n");
    parse(paths, fs,
        "rule cat\n"
        "  command = cat\n"
        "subninja test.ninja\n");
  }

  SECTION("DuplicateRuleInDifferentSubninjasWithInclude") {
    // Test that rules are scoped to subninjas even with includes.
    writeFile(fs, "rules.ninja",
        "rule cat\n"
        "  command = cat\n");
    writeFile(fs, "test.ninja",
        "include rules.ninja\n"
        "build x : cat\n");
    parse(paths, fs,
        "include rules.ninja\n"
        "subninja test.ninja\n"
        "build y : cat\n");
  }

  SECTION("Include") {
    writeFile(fs, "include.ninja",
        "var = inner\n");
    const auto step = parseStep(paths, fs,
        "var = outer\n"
        "include include.ninja\n"
        "rule r\n"
        "  command = $var\n"
        "build out: r\n");

    CHECK(step.command == "inner");
  }

  SECTION("BrokenInclude") {
    writeFile(fs, "include.ninja",
        "build\n");
    CHECK(parseError(paths, fs, "include include.ninja\n") ==
        "include.ninja:1: expected path\n"
        "build\n"
        "     ^ near here");
  }

  SECTION("Implicit") {
    const auto step = parseStep(paths, fs,
        "rule cat\n"
        "  command = cat $in > $out\n"
        "build foo: cat bar | baz\n");

    CHECK(step.command == "cat bar > foo");
    REQUIRE(step.inputs.size() == 1);
    CHECK(step.inputs[0].original() == "bar");
    REQUIRE(step.implicit_inputs.size() == 1);
    CHECK(step.implicit_inputs[0].original() == "baz");
    CHECK(step.dependencies.empty());
  }

  SECTION("OrderOnly") {
    const auto step = parseStep(paths, fs,
        "rule cat\n  command = cat $in > $out\n"
        "build foo: cat bar || baz\n");

    REQUIRE(step.inputs.size() == 1);
    CHECK(step.inputs[0].original() == "bar");
    CHECK(step.implicit_inputs.empty());
    REQUIRE(step.dependencies.size() == 1);
    CHECK(step.dependencies[0].original() == "baz");
  }

  SECTION("DefaultDefault") {
    const auto manifest = parse(paths, fs,
        "rule cat\n  command = cat $in > $out\n"
        "build a: cat foo\n"
        "build b: cat foo\n"
        "build c: cat foo\n"
        "build d: cat foo\n");
    CHECK(manifest.defaults.empty());
  }

  SECTION("DefaultStatements") {
    const auto manifest = parse(paths, fs,
        "rule cat\n  command = cat $in > $out\n"
        "build a: cat foo\n"
        "build b: cat foo\n"
        "build c: cat foo\n"
        "build d: cat foo\n"
        "third = c\n"
        "default a b\n"
        "default $third\n");

    REQUIRE(manifest.defaults.size() == 3);
    CHECK(manifest.defaults[0].original() == "a");
    CHECK(manifest.defaults[1].original() == "b");
    CHECK(manifest.defaults[2].original() == "c");
  }

  SECTION("UTF8") {
    const auto manifest = parse(paths, fs,
        "rule utf8\n"
        "  command = true\n"
        "  description = compilaci\xC3\xB3\n");
  }

  SECTION("CRLF") {
    parse(paths, fs, "# comment with crlf\r\n");
    parse(paths, fs, "foo = foo\nbar = bar\r\n");
    parse(paths, fs, 
        "pool link_pool\r\n"
        "  depth = 15\r\n\r\n"
        "rule xyz\r\n"
        "  command = something$expand \r\n"
        "  description = YAY!\r\n");
  }

  SECTION("VariableExpansionTime") {
    SECTION("EagerlyEvaluateStepBindings") {
      const auto step = parseStep(paths, fs,
          "variable = old\n"
          "rule cat\n"
          "  command = echo $out $variable\n"
          "  description = Hi $variable\n"
          "build result: cat\n"
          "  description = $variable\n"
          "variable = my_var\n");

      CHECK(step.description == "old");
    }

    SECTION("EagerlyEvaluateInputs") {
      const auto step = parseStep(paths, fs,
          "variable = old\n"
          "rule cat\n"
          "  command = echo $out $variable\n"
          "  description = Hi $variable\n"
          "build result: cat $variable\n"
          "variable = new\n");

      REQUIRE(step.inputs.size() == 1);
      CHECK(step.inputs[0].original() == "old");
    }

    SECTION("EagerlyEvaluateOutputs") {
      const auto step = parseStep(paths, fs,
          "variable = old\n"
          "rule cat\n"
          "  command = echo $out $variable\n"
          "  description = Hi $variable\n"
          "build $variable: cat in\n"
          "variable = new\n");

      REQUIRE(step.outputs.size() == 1);
      CHECK(step.outputs[0].original() == "old");
    }

    SECTION("EagerlyEvaluateImplicit") {
      const auto step = parseStep(paths, fs,
          "variable = old\n"
          "rule cat\n"
          "  command = echo $out $variable\n"
          "  description = Hi $variable\n"
          "build result: cat | $variable\n"
          "variable = new\n");

      REQUIRE(step.implicit_inputs.size() == 1);
      CHECK(step.implicit_inputs[0].original() == "old");
    }

    SECTION("EagerlyEvaluateOrderOnly") {
      const auto step = parseStep(paths, fs,
          "variable = old\n"
          "rule cat\n"
          "  command = echo $out $variable\n"
          "  description = Hi $variable\n"
          "build result: cat || $variable\n"
          "variable = new\n");

      REQUIRE(step.dependencies.size() == 1);
      CHECK(step.dependencies[0].original() == "old");
    }

    SECTION("EagerlyEvaluatePoolName") {
      const auto step = parseStep(paths, fs,
          "variable = old\n"
          "pool old\n"
          "  depth = 1\n"
          "pool new\n"
          "  depth = 1\n"
          "rule cat\n"
          "  command = echo $out\n"
          "  pool = $variable\n"
          "build result: cat\n"
          "variable = new\n");

      CHECK(step.pool_name == "old");
    }

    SECTION("LazilyEvaluateRuleBindings") {
      const auto step = parseStep(paths, fs,
          "variable = old\n"
          "rule cat\n"
          "  command = echo $out $variable\n"
          "  description = Hi $variable\n"
          "  restat = $other_var\n"
          "  generator = $other_var\n"
          "  depfile = $variable\n"
          "  rspfile = $variable\n"
          "  rspfile_content = $variable\n"
          "build result: cat || $variable\n"
          "variable = new\n"
          "other_var = new2\n");

      CHECK(step.command == "echo result new");
      CHECK(step.description == "Hi new");
      CHECK(step.restat);
      CHECK(step.generator);
      CHECK(step.depfile.original() == "new");
      CHECK(step.rspfile.original() == "new");
      CHECK(step.rspfile_content == "new");
    }
  }
}

}  // namespace shk
