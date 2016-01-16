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

#include "lexer.h"

#include <stdio.h>

#include "eval_env.h"
#include "util.h"

namespace shk {

std::string Lexer::error(const std::string& message) const {
  std::string err;

  // Compute line/column.
  int line = 1;
  const char* context = _input._str;
  for (const char* p = _input._str; p < _last_token; ++p) {
    if (*p == '\n') {
      ++line;
      context = p + 1;
    }
  }
  int col = _last_token ? (int)(_last_token - context) : 0;

  char buf[1024];
  snprintf(buf, sizeof(buf), "%s:%d: ", _filename.asString().c_str(), line);
  err = buf;
  err += message + "\n";

  // Add some context to the message.
  const int kTruncateColumn = 72;
  if (col > 0 && col < kTruncateColumn) {
    int len;
    bool truncated = true;
    for (len = 0; len < kTruncateColumn; ++len) {
      if (context[len] == 0 || context[len] == '\n') {
        truncated = false;
        break;
      }
    }
    err += std::string(context, len);
    if (truncated)
      err += "...";
    err += "\n";
    err += std::string(col, ' ');
    err += "^ near here";
  }

  return err;
}

Lexer::Lexer(const char* input) {
  start("input", input);
}

void Lexer::start(StringPiece filename, StringPiece input) {
  _filename = filename;
  _input = input;
  _ofs = _input._str;
  _last_token = NULL;
}

const char* Lexer::tokenName(Token t) {
  switch (t) {
  case ERROR:    return "lexing error";
  case BUILD:    return "'build'";
  case COLON:    return "':'";
  case DEFAULT:  return "'default'";
  case EQUALS:   return "'='";
  case IDENT:    return "identifier";
  case INCLUDE:  return "'include'";
  case INDENT:   return "indent";
  case NEWLINE:  return "newline";
  case PIPE2:    return "'||'";
  case PIPE:     return "'|'";
  case POOL:     return "'pool'";
  case RULE:     return "'rule'";
  case SUBNINJA: return "'subninja'";
  case TEOF:     return "eof";
  }
  return NULL;  // not reached
}

const char* Lexer::tokenErrorHint(Token expected) {
  switch (expected) {
  case COLON:
    return " ($ also escapes ':')";
  default:
    return "";
  }
}

std::string Lexer::describeLastError() {
  if (_last_token) {
    switch (_last_token[0]) {
    case '\t':
      return "tabs are not allowed, use spaces";
    }
  }
  return "lexing error";
}

void Lexer::unreadToken() {
  _ofs = _last_token;
}

Lexer::Token Lexer::readToken() {
  const char* p = _ofs;
  const char* q;
  const char* start;
  Lexer::Token token;
  for (;;) {
    start = p;
    /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:define:YYCURSOR = p;
    re2c:define:YYMARKER = q;
    re2c:yyfill:enable = 0;

    nul = "\000";
    simple_varname = [a-zA-Z0-9_-]+;
    varname = [a-zA-Z0-9_.-]+;

    [ ]*"#"[^\000\n]*"\n" { continue; }
    [ ]*"\r\n" { token = NEWLINE;  break; }
    [ ]*"\n"   { token = NEWLINE;  break; }
    [ ]+       { token = INDENT;   break; }
    "build"    { token = BUILD;    break; }
    "pool"     { token = POOL;     break; }
    "rule"     { token = RULE;     break; }
    "default"  { token = DEFAULT;  break; }
    "="        { token = EQUALS;   break; }
    ":"        { token = COLON;    break; }
    "||"       { token = PIPE2;    break; }
    "|"        { token = PIPE;     break; }
    "include"  { token = INCLUDE;  break; }
    "subninja" { token = SUBNINJA; break; }
    varname    { token = IDENT;    break; }
    nul        { token = TEOF;     break; }
    [^]        { token = ERROR;    break; }
    */
  }

  _last_token = start;
  _ofs = p;
  if (token != NEWLINE && token != TEOF)
    eatWhitespace();
  return token;
}

bool Lexer::peekToken(Token token) {
  Token t = readToken();
  if (t == token)
    return true;
  unreadToken();
  return false;
}

void Lexer::eatWhitespace() {
  const char* p = _ofs;
  const char* q;
  for (;;) {
    _ofs = p;
    /*!re2c
    [ ]+    { continue; }
    "$\r\n" { continue; }
    "$\n"   { continue; }
    nul     { break; }
    [^]     { break; }
    */
  }
}

bool Lexer::readIdent(std::string* out) {
  const char* p = _ofs;
  for (;;) {
    const char* start = p;
    /*!re2c
    varname {
      out->assign(start, p - start);
      break;
    }
    [^] { return false; }
    */
  }
  _ofs = p;
  eatWhitespace();
  return true;
}

void Lexer::readEvalString(EvalString* eval, bool path) throw(ParseError) {
  const char* p = _ofs;
  const char* q;
  const char* start;
  for (;;) {
    start = p;
    /*!re2c
    [^$ :\r\n|\000]+ {
      eval->addText(StringPiece(start, p - start));
      continue;
    }
    "\r\n" {
      if (path)
        p = start;
      break;
    }
    [ :|\n] {
      if (path) {
        p = start;
        break;
      } else {
        if (*start == '\n')
          break;
        eval->addText(StringPiece(start, 1));
        continue;
      }
    }
    "$$" {
      eval->addText(StringPiece("$", 1));
      continue;
    }
    "$ " {
      eval->addText(StringPiece(" ", 1));
      continue;
    }
    "$\r\n"[ ]* {
      continue;
    }
    "$\n"[ ]* {
      continue;
    }
    "${"varname"}" {
      eval->addSpecial(StringPiece(start + 2, p - start - 3));
      continue;
    }
    "$"simple_varname {
      eval->addSpecial(StringPiece(start + 1, p - start - 1));
      continue;
    }
    "$:" {
      eval->addText(StringPiece(":", 1));
      continue;
    }
    "$". {
      _last_token = start;
      throw ParseError(error("bad $-escape (literal $ must be written as $$)"));
    }
    nul {
      _last_token = start;
      throw ParseError(error("unexpected EOF"));
    }
    [^] {
      _last_token = start;
      throw ParseError(error(describeLastError()));
    }
    */
  }
  _last_token = start;
  _ofs = p;
  if (path)
    eatWhitespace();
  // Non-path strings end in newlines, so there's no whitespace to eat.
}

}  // namespace shk
