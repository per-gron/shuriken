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

#include <catch.hpp>

#include "manifest/eval_env.h"
#include "manifest/lexer.h"

namespace shk {
namespace {

std::string readVarValueError(Lexer &lexer, EvalString* value) {
  try {
    lexer.readVarValue(value);
    CHECK_THROWS((void)0);
    return "";
  } catch (ParseError &error) {
    return error.what();
  }
}

}  // anonymous namespace

TEST_CASE("Lexer") {
  SECTION("readVarValue") {
    Lexer lexer("plain text $var $VaR ${x}\n");
    EvalString eval;
    lexer.readVarValue(&eval);
    CHECK("[plain text ][$var][ ][$VaR][ ][$x]" == eval.serialize());
  }

  SECTION("ReadEvalStringEscapes") {
    Lexer lexer("$ $$ab c$: $\ncde\n");
    EvalString eval;
    lexer.readVarValue(&eval);
    CHECK("[ $ab c: cde]" == eval.serialize());
  }

  SECTION("readIdent") {
    Lexer lexer("foo baR baz_123 foo-bar");
    CHECK("foo" == lexer.readIdent(""));
    CHECK("baR" == lexer.readIdent(""));
    CHECK("baz_123" == lexer.readIdent(""));
    CHECK("foo-bar" == lexer.readIdent(""));
  }

  SECTION("readIdentCurlies") {
    // Verify that readIdent includes dots in the name,
    // but in an expansion $bar.dots stops at the dot.
    Lexer lexer("foo.dots $bar.dots ${bar.dots}\n");
    CHECK("foo.dots" == lexer.readIdent(""));

    EvalString eval;
    lexer.readVarValue(&eval);
    CHECK("[$bar][.dots ][$bar.dots]" == eval.serialize());
  }

  SECTION("Error") {
    Lexer lexer("foo$\nbad $");
    EvalString eval;
    const auto err = readVarValueError(lexer, &eval);
    const auto expected_err =
        "input:2: bad $-escape (literal $ must be written as $$)\n"
        "bad $\n"
        "    ^ near here";
    CHECK(err == expected_err);
  }

  SECTION("CommentEOF") {
    // Verify we don't run off the end of the string when the EOF is
    // mid-comment.
    Lexer lexer("# foo");
    Lexer::Token token = lexer.readToken();
    CHECK(Lexer::ERROR == token);
  }

  SECTION("Tabs") {
    // Verify we print a useful error on a disallowed character.
    Lexer lexer("   \tfoobar");
    Lexer::Token token = lexer.readToken();
    CHECK(Lexer::INDENT == token);
    token = lexer.readToken();
    CHECK(Lexer::ERROR == token);
    CHECK("tabs are not allowed, use spaces" == lexer.describeLastError());
  }
}

}  // namespace shk
