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

#include "parse_error.h"
#include "string_piece.h"

// Windows may #define ERROR.
#ifdef ERROR
#undef ERROR
#endif

namespace shk {

struct EvalString;

struct Lexer {
  Lexer() {}
  /**
   * Helper ctor useful for tests.
   */
  explicit Lexer(const char *input);

  enum Token {
    ERROR,
    BUILD,
    COLON,
    DEFAULT,
    EQUALS,
    IDENT,
    INCLUDE,
    INDENT,
    NEWLINE,
    PIPE,
    PIPE2,
    POOL,
    RULE,
    SUBNINJA,
    TEOF,
  };

  /**
   * Return a human-readable form of a token, used in error messages.
   */
  static const char *tokenName(Token t);

  /**
   * Return a human-readable token hint, used in error messages.
   */
  static const char *tokenErrorHint(Token expected);

  /**
   * If the last token read was an ERROR token, provide more info
   * or the empty string.
   */
  std::string describeLastError();

  /**
   * Start parsing some input.
   */
  void start(StringPiece filename, StringPiece input);

  /**
   * Read a Token from the Token enum.
   */
  Token readToken();

  /**
   * Rewind to the last read Token.
   */
  void unreadToken();

  /**
   * If the next token is \a token, read it and return true.
   */
  bool peekToken(Token token);

  /**
   * Read a simple identifier (a rule or variable name).
   *
   * \a ident_type is a string describing the expected type, used in the error
   * message of the ParseError on failure.
   *
   * Throws ParseError if a name can't be read.
   */
  std::string readIdent(const char *ident_type) throw(ParseError);

  /**
   * Read a path (complete with $escapes).
   *
   * Returned path may be empty if a delimiter (space, newline) is hit.
   */
  void readPath(EvalString *path) throw(ParseError) {
    return readEvalString(path, true);
  }

  /**
   * Read the value side of a var = value line (complete with $escapes).
   */
  void readVarValue(EvalString *value) throw(ParseError) {
    return readEvalString(value, false);
  }

  /**
   * Construct an error message with context.
   */
  std::string error(const std::string& message) const;

  /**
   * Construct and throw an error message with context.
   */
  void throwError(const std::string& message) const {
    throw ParseError(error(message));
  }

private:
  /**
   * Skip past whitespace (called after each read token/ident/etc.).
   */
  void eatWhitespace();

  /**
   * Read a $-escaped string.
   */
  void readEvalString(EvalString* eval, bool path) throw(ParseError);

  StringPiece _filename;
  StringPiece _input;
  const char *_ofs;
  const char *_last_token;
};

}  // namespace shk
