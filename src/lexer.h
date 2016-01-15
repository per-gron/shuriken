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

#include "string_piece.h"

// Windows may #define ERROR.
#ifdef ERROR
#undef ERROR
#endif

namespace shk {

struct EvalString;

struct Lexer {
  Lexer() {}
  /// Helper ctor useful for tests.
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
  static const char* tokenName(Token t);

  /**
   * Return a human-readable token hint, used in error messages.
   */
  static const char* tokenErrorHint(Token expected);

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
   * Returns false if a name can't be read.
   */
  bool readIdent(std::string* out);

  /**
   * Read a path (complete with $escapes).
   * Returns false only on error, returned path may be empty if a delimiter
   * (space, newline) is hit.
   */
  bool readPath(EvalString* path, std::string* err) {
    return readEvalString(path, true, err);
  }

  /**
   * Read the value side of a var = value line (complete with $escapes).
   * Returns false only on error.
   */
  bool readVarValue(EvalString* value, std::string* err) {
    return readEvalString(value, false, err);
  }

  /**
   * Construct an error message with context.
   */
  bool error(const std::string& message, std::string* err);

private:
  /**
   * Skip past whitespace (called after each read token/ident/etc.).
   */
  void eatWhitespace();

  /// Read a $-escaped string.
  bool readEvalString(EvalString* eval, bool path, std::string* err);

  StringPiece _filename;
  StringPiece _input;
  const char *_ofs;
  const char *_last_token;
};

}  // namespace shk
