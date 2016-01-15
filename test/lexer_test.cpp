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

#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "eval_env.h"
#include "lexer.h"

TEST_CASE("Lexer") {
  SECTION("readVarValue") {
    Lexer lexer("plain text $var $VaR ${x}\n");
    EvalString eval;
    std::string err;
    CHECK(lexer.readVarValue(&eval, &err));
    CHECK("" == err);
    CHECK("[plain text ][$var][ ][$VaR][ ][$x]" == eval.serialize());
  }

  SECTION("ReadEvalStringEscapes") {
    Lexer lexer("$ $$ab c$: $\ncde\n");
    EvalString eval;
    std::string err;
    CHECK(lexer.readVarValue(&eval, &err));
    CHECK("" == err);
    CHECK("[ $ab c: cde]" == eval.serialize());
  }

  SECTION("readIdent") {
    Lexer lexer("foo baR baz_123 foo-bar");
    std::string ident;
    CHECK(lexer.readIdent(&ident));
    CHECK("foo" == ident);
    CHECK(lexer.readIdent(&ident));
    CHECK("baR" == ident);
    CHECK(lexer.readIdent(&ident));
    CHECK("baz_123" == ident);
    CHECK(lexer.readIdent(&ident));
    CHECK("foo-bar" == ident);
  }

  SECTION("readIdentCurlies") {
    // Verify that readIdent includes dots in the name,
    // but in an expansion $bar.dots stops at the dot.
    Lexer lexer("foo.dots $bar.dots ${bar.dots}\n");
    std::string ident;
    CHECK(lexer.readIdent(&ident));
    CHECK("foo.dots" == ident);

    EvalString eval;
    std::string err;
    CHECK(lexer.readVarValue(&eval, &err));
    CHECK("" == err);
    CHECK("[$bar][.dots ][$bar.dots]" == eval.serialize());
  }

  SECTION("Error") {
    Lexer lexer("foo$\nbad $");
    EvalString eval;
    std::string err;
    CHECK(!lexer.readVarValue(&eval, &err));
    CHECK("input:2: bad $-escape (literal $ must be written as $$)\n"
          "bad $\n"
          "    ^ near here" == err);
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
