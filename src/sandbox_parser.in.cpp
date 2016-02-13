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

#include "sandbox_parser.h"

#include "parse_error.h"
#include "string_piece.h"
#include "util.h"

namespace shk {

SandboxIgnores SandboxIgnores::defaults() {
  SandboxIgnores result;
  result.file_access = {
      "/dev/null",
      "/dev/random",
      "/dev/urandom",
      "/dev/dtracehelper",
      "/dev/tty",
      "/",
      "/usr",
      "/etc",
      "/Users",
      "/Applications",
      "/tmp",
      "/private/tmp",
      "/private" };
  result.network_access = {
      "/private/var/run/syslog" };
  return result;
}

namespace {

bool fileAccessIgnored(
    const SandboxIgnores &ignores,
    const std::string &path) {
  return ignores.file_access.count(path) != 0;
}

bool networkAccessIgnored(
    const SandboxIgnores &ignores,
    const std::string &path) {
  return ignores.network_access.count(path) != 0;
}

struct ParsingContext {
  ParsingContext(char *in, const char *end)
      : in(in), end(end) {}

  char *in;
  const char *end;

  bool atEnd() const {
    return in == end;
  }
};

NORETURN void parseError(ParsingContext &context, const std::string &error) {
  // Might be good to include some more context here
  throw ParseError(error);
}

void readToEOL(ParsingContext &context) {
  char *p = context.in;
  for (;;) {
    context.in = p;
    /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:define:YYCURSOR = p;
    re2c:define:YYMARKER = q;
    re2c:yyfill:enable = 0;

    [\n]       { break;    }
    [\000]     { break;    }
    [^]        { continue; }
    */
  }
}

enum class StatementToken {
  VERSION,
  DENY,
  ALLOW,
};

StatementToken readStatementToken(ParsingContext &context) throw(ParseError) {
  char *p = context.in;
  char *q;
  StatementToken token;
  for (;;) {
    /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:define:YYCURSOR = p;
    re2c:define:YYMARKER = q;
    re2c:yyfill:enable = 0;

    [ ]+      { continue;        }
    "version" { token = StatementToken::VERSION; break; }
    "deny"    { token = StatementToken::DENY;    break; }
    "allow"   { token = StatementToken::ALLOW;   break; }
    [^]       { parseError(context, "Encountered unexpected statement token"); }
    */
  }

  context.in = p;
  return token;
}

enum class AllowToken {
  FILE_READ_METADATA,
  FILE_READ_DATA,
  FILE_WRITE_CREATE,
  FILE_WRITE_DATA,
  FILE_WRITE_FLAGS,
  FILE_WRITE_MODE,
  FILE_WRITE_OWNER,
  FILE_WRITE_SETUGID,
  FILE_REVOKE,
  FILE_WRITE_UNLINK,

  // Conditionally allowed actions
  FILE_IOCTL,  // Allowed only when in SandboxIgnores
  NETWORK_OUTBOUND,  // Allowed only when in SandboxIgnores

  // Always allowed actions
  SYSCTL_READ,

  PROCESS_STAR,
  PROCESS_EXEC,
  PROCESS_EXEC_STAR,
  PROCESS_FORK,

  // Always disallowed actions
  SIGNAL,

  NETWORK_STAR,
  NETWORK_INBOUND,
  NETWORK_BIND,

  FILE_IOCTL_WRITE_XATTR,  // Not supported initially
  FILE_READ_XATTR,  // Not supported initially

  FILE_WRITE_UNMOUNT,
  FILE_WRITE_MOUNT,
  FILE_WRITE_TIMES,

  SYSCTL_STAR,
  SYSCTL_WRITE,

  SYSTEM_STAR,
  SYSTEM_ACCT,
  SYSTEM_AUDIT,
  SYSTEM_FSCTL,
  SYSTEM_LCID,
  SYSTEM_MAC_LABEL,
  SYSTEM_NFSSVC,
  SYSTEM_REBOOT,
  SYSTEM_SET_TIME,
  SYSTEM_SOCKET,
  SYSTEM_SWAP,
  SYSTEM_WRITE_BOOTSTRAP,

  JOB_CREATION,

  IPC_STAR,
  IPC_POSIX_STAR,
  IPC_POSIX_SEM,
  IPC_POSIX_SHM,
  IPC_SYSV_STAR,
  IPC_SYSV_MSG,
  IPC_SYSV_SEM,
  IPC_SYSV_SHM,

  MACH_STAR,
  MACH_PER_USER_LOOKUP,
  MACH_BOOTSTRAP,
  MACH_LOOKUP,
  MACH_PRIV_STAR,
  MACH_PRIV_HOST_PORT,
  MACH_PRIV_TASK_PORT,
  MACH_TASK_NAME,
};

AllowToken readAllowToken(ParsingContext &context) throw(ParseError) {
  char *p = context.in;
  char *q;
  AllowToken token;
  for (;;) {
    /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:define:YYCURSOR = p;
    re2c:define:YYMARKER = q;
    re2c:yyfill:enable = 0;

    "file-read-metadata"     { token = AllowToken::FILE_READ_METADATA;     break; }
    "file-read-data"         { token = AllowToken::FILE_READ_DATA;         break; }
    "file-write-create"      { token = AllowToken::FILE_WRITE_CREATE;      break; }
    "file-write-data"        { token = AllowToken::FILE_WRITE_DATA;        break; }
    "file-write-flags"       { token = AllowToken::FILE_WRITE_FLAGS;       break; }
    "file-write-mode"        { token = AllowToken::FILE_WRITE_MODE;        break; }
    "file-write-owner"       { token = AllowToken::FILE_WRITE_OWNER;       break; }
    "file-write-setugid"     { token = AllowToken::FILE_WRITE_SETUGID;     break; }
    "file-revoke"            { token = AllowToken::FILE_REVOKE;            break; }
    "file-write-unlink"      { token = AllowToken::FILE_WRITE_UNLINK;      break; }
    "file-ioctl"             { token = AllowToken::FILE_IOCTL;             break; }
    "network-outbound"       { token = AllowToken::NETWORK_OUTBOUND;       break; }
    "sysctl-read"            { token = AllowToken::SYSCTL_READ;            break; }
    "process\*"              { token = AllowToken::PROCESS_STAR;           break; }
    "process-exec"           { token = AllowToken::PROCESS_EXEC;           break; }
    "process-exec\*"         { token = AllowToken::PROCESS_EXEC_STAR;      break; }
    "process-fork"           { token = AllowToken::PROCESS_FORK;           break; }
    "signal"                 { token = AllowToken::SIGNAL;                 break; }
    "network\*"              { token = AllowToken::NETWORK_STAR;           break; }
    "network-inbound"        { token = AllowToken::NETWORK_INBOUND;        break; }
    "network-bind"           { token = AllowToken::NETWORK_BIND;           break; }
    "file-ioctl-write-xattr" { token = AllowToken::FILE_IOCTL_WRITE_XATTR; break; }
    "file-read-xattr"        { token = AllowToken::FILE_READ_XATTR;        break; }
    "file-write-unmount"     { token = AllowToken::FILE_WRITE_UNMOUNT;     break; }
    "file-write-mount"       { token = AllowToken::FILE_WRITE_MOUNT;       break; }
    "file-write-times"       { token = AllowToken::FILE_WRITE_TIMES;       break; }
    "sysctl\*"               { token = AllowToken::SYSCTL_STAR;            break; }
    "sysctl-write"           { token = AllowToken::SYSCTL_WRITE;           break; }
    "system\*"               { token = AllowToken::SYSTEM_STAR;            break; }
    "system-acct"            { token = AllowToken::SYSTEM_ACCT;            break; }
    "system-audit"           { token = AllowToken::SYSTEM_AUDIT;           break; }
    "system-fsctl"           { token = AllowToken::SYSTEM_FSCTL;           break; }
    "system-lcid"            { token = AllowToken::SYSTEM_LCID;            break; }
    "system-mac-label"       { token = AllowToken::SYSTEM_MAC_LABEL;       break; }
    "system-nfssvc"          { token = AllowToken::SYSTEM_NFSSVC;          break; }
    "system-reboot"          { token = AllowToken::SYSTEM_REBOOT;          break; }
    "system-set-time"        { token = AllowToken::SYSTEM_SET_TIME;        break; }
    "system-socket"          { token = AllowToken::SYSTEM_SOCKET;          break; }
    "system-swap"            { token = AllowToken::SYSTEM_SWAP;            break; }
    "system-write-bootstrap" { token = AllowToken::SYSTEM_WRITE_BOOTSTRAP; break; }
    "job-creation"           { token = AllowToken::JOB_CREATION;           break; }
    "ipc\*"                  { token = AllowToken::IPC_STAR;               break; }
    "ipc-posix\*"            { token = AllowToken::IPC_POSIX_STAR;         break; }
    "ipc-posix-sem"          { token = AllowToken::IPC_POSIX_SEM;          break; }
    "ipc-posix-shm"          { token = AllowToken::IPC_POSIX_SHM;          break; }
    "ipc-sysv\*"             { token = AllowToken::IPC_SYSV_STAR;          break; }
    "ipc-sysv-msg"           { token = AllowToken::IPC_SYSV_MSG;           break; }
    "ipc-sysv-sem"           { token = AllowToken::IPC_SYSV_SEM;           break; }
    "ipc-sysv-shm"           { token = AllowToken::IPC_SYSV_SHM;           break; }
    "mach\*"                 { token = AllowToken::MACH_STAR;              break; }
    "mach-per-user-lookup"   { token = AllowToken::MACH_PER_USER_LOOKUP;   break; }
    "mach-bootstrap"         { token = AllowToken::MACH_BOOTSTRAP;         break; }
    "mach-lookup"            { token = AllowToken::MACH_LOOKUP;            break; }
    "mach-priv\*"            { token = AllowToken::MACH_PRIV_STAR;         break; }
    "mach-priv-host-port"    { token = AllowToken::MACH_PRIV_HOST_PORT;    break; }
    "mach-priv-task-port"    { token = AllowToken::MACH_PRIV_TASK_PORT;    break; }
    "mach-task-name"         { token = AllowToken::MACH_TASK_NAME;         break; }
    [^]                      { parseError(context, "Encountered unexpected allow token"); }
    */
  }

  context.in = p;
  return token;
}

/**
 * Reads an opening paren or goes to the end of the input. Throws only if it
 * encounters something else than a comment, the end of input or a paren.
 */
bool readOpeningParen(ParsingContext &context) throw(ParseError) {
  char *p = context.in;
  bool found_paren = false;
  for (;;) {
    context.in = p;
    /*!re2c
    [ \n]+              { continue;                                }
    ";"[^\000\n]*       { continue;                                }
    "\("                { context.in++; found_paren = true; break; }
    "\000"              { break;                                   }
    [^]                 { parseError(context, "Encountered unexpected token; expected ("); }
    */
  }
  return found_paren;
}

/**
 * Consume the string "(literal ", possibly with extra whitespace.
 */
void readLiteralPrefix(ParsingContext &context) throw(ParseError) {
  char *p = context.in;
  char *q;
  for (;;) {
    /*!re2c
    [ ]+"("[ ]*"literal"[ ]+"\"" { break; }
    [^] { parseError(context, "Encountered unexpected token; expected (literal"); }
    */
  }
  context.in = p;
}

/**
 * Converts a hex char (upper or lower case) to an int. Does not do bound
 * checking.
 */
int hexToInt(char chr) {
  const auto upper = toupper(chr);
  return upper <= '9' ? upper - '0' : upper - 'A' + 10;
}

/**
 * Consume a string literal of the form '(literal "/a/b/c"' (does not consume
 * the final end paren).
 *
 * Supports string escapes in the same format as TinyScheme (see readstrexp in
 * scheme.c of TinyScheme).
 */
StringPiece readLiteral(ParsingContext &context) throw(ParseError) {
  readLiteralPrefix(context);

  // filename: Start of the current parsed filename.
  char *filename = context.in;
  // out: Current output point (typically same as context.in, but can fall
  // behind as we de-escape backslashes).
  char *out = context.in;
  char *q;

  for (;;) {
    // start: beginning of the current parsed span.
    const char* start = context.in;
    /*!re2c
    re2c:define:YYCTYPE = "char";
    re2c:define:YYCURSOR = context.in;

    re2c:yyfill:enable = 0;

    nul = "\000";
    escape = [ \\#*[|];

    "\\n" {
      // De-escape newline
      *out++ = '\n';
      continue;
    }
    "\\t" {
      // De-escape tab
      *out++ = '\t';
      continue;
    }
    "\\r" {
      // De-escape line feed
      *out++ = '\r';
      continue;
    }
    "\\\"" {
      // De-escape double quote
      *out++ = '"';
      continue;
    }
    "\\x"[a-fA-F0-9]{2} {
      // De-escape hex escape
      const auto a = *(context.in - 2);
      const auto b = *(context.in - 1);
      *out++ = (hexToInt(a) << 4) + hexToInt(b);
      continue;
    }
    "\\"[0-7]{2} {
      // De-escape two digit octal escape
      //
      // Looking at TinyScheme's parser, it does not look like it supports
      // single digit hex escape sequences.
      const auto a = *(context.in - 2);
      const auto b = *(context.in - 1);
      *out++ = ((a - '0') << 3) + (b - '0');
      continue;
    }
    "\\"[0-7] {
      // De-escape single digit octal escape
      const auto a = *(context.in - 1);
      *out++ = a - '0';
      continue;
    }
    "\\" {
      parseError(context, "Encountered unexpected escape sequence");
    }

    [^\n\"\\]+ {
      // Got a span of plain text.
      const auto len = context.in - start;
      // Need to shift it over if we're overwriting backslashes.
      if (out < start) {
        memmove(out, start, len);
      }
      out += len;
      continue;
    }
    [\"] {
      break;
    }
    [^] {
      parseError(context, "Encountered unexpected end of input within string literal");
    }
    */
  }
  return StringPiece(filename, out - filename);
}

std::string readPath(ParsingContext &context) throw(ParseError) {
  return readLiteral(context).asString();
}

/**
 * Read at least one space
 */
void readWhitespace(ParsingContext &context) throw(ParseError) {
  char *p = context.in;
  for (;;) {
    /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:define:YYCURSOR = p;
    re2c:define:YYMARKER = q;
    re2c:yyfill:enable = 0;

    [ \n]+ { break;    }
    [^]    { parseError(context, "Expected whitespace"); }
    */
  }
  context.in = p;
}

void readAllow(
    const SandboxIgnores &ignores,
    std::unordered_set<std::string> &must_create,
    ParsingContext &context,
    SandboxResult &result) throw(ParseError) {
  const char *token_start = context.in;
  const auto token = readAllowToken(context);
  StringPiece token_slice(token_start, context.in - token_start);

  switch (token) {
  case AllowToken::FILE_WRITE_CREATE: {
    auto path = readLiteral(context).asString();
    if (!fileAccessIgnored(ignores, path)) {
      // Just deleting the file from the set of read files is kind of wrong but
      // I don't think the sandbox tracing allows for sufficient precision to
      // know when this is or is not legit.
      result.read.erase(path);
      result.created.insert(std::move(path));
    }
    readToEOL(context);
    break;
  }

  case AllowToken::FILE_WRITE_UNLINK: {
    const auto path = readPath(context);
    if (!fileAccessIgnored(ignores, path)) {
      if (result.created.count(path) == 0) {
        // Build steps must only create files, it cannot remove them (because it
        // is not possible to clean up after that action). Because of this, it
        // is only allowed to remove a file that a build step has previously
        // created or to remove files that it is about to overwrite.
        //
        // TODO(peck): Ideally it should not be allowed to delete a file that
        // the build step has already created but I do not know how to make that
        // work in practice. For example, if an invocation log entry was not
        // written for a build step, the build step should still be allowed to
        // overwrite the outputs of the previous (possibly failed) build step
        // invocation.
        must_create.insert(path);
      }
      result.created.erase(path);
    }
    readToEOL(context);
    break;
  }

  case AllowToken::FILE_WRITE_DATA:
  case AllowToken::FILE_WRITE_FLAGS:
  case AllowToken::FILE_WRITE_MODE:
  case AllowToken::FILE_WRITE_OWNER:
  case AllowToken::FILE_WRITE_SETUGID:
  case AllowToken::FILE_WRITE_TIMES:
  case AllowToken::FILE_REVOKE: {
    auto path = readPath(context);
    if (!fileAccessIgnored(ignores, path)) {
      // Just deleting the file from the set of read files is kind of wrong but
      // I don't think the sandbox tracing allows for sufficient precision to
      // know when this is or is not legit.
      result.read.erase(path);
      result.created.insert(std::move(path));
    }
    readToEOL(context);
    break;
  }

  case AllowToken::FILE_READ_METADATA:
  case AllowToken::FILE_READ_DATA:
  case AllowToken::PROCESS_STAR:
  case AllowToken::PROCESS_EXEC:
  case AllowToken::PROCESS_EXEC_STAR: {
    const auto path = readPath(context);
    if (!fileAccessIgnored(ignores, path)) {
      if (result.created.count(path) == 0) {
        // It is ok for the process to read from a file it created, but only count
        // files as read if they were not created by the process.
        const auto just_metadata = token == AllowToken::FILE_READ_METADATA;
        if (just_metadata) {
          result.read.emplace(path, DependencyType::IGNORE_IF_DIRECTORY);
        } else {
          result.read[path] = DependencyType::ALWAYS;
        }
      }
    }
    readToEOL(context);
    break;
  }

  case AllowToken::FILE_IOCTL: {
    const auto path = readLiteral(context).asString();
    if (!fileAccessIgnored(ignores, path)) {
      result.violations.emplace_back(
          "Process used ioctl on illegal path " + path);
    }
    readToEOL(context);
    break;
  }

  case AllowToken::NETWORK_OUTBOUND: {
    try {
      const auto path = readLiteral(context).asString();
      if (!networkAccessIgnored(ignores, path)) {
        result.violations.emplace_back(
            "Process opened network connection on illegal path " + path);
      }
    } catch (ParseError &) {
      // Failed to read path. Might be a network address such as
      // (remote tcp4 "*:80"). These are disallowed
      result.violations.emplace_back(
          "Process performed disallowed action network-outbound");
    }
    readToEOL(context);
    break;
  }

  case AllowToken::SYSCTL_READ:
  case AllowToken::PROCESS_FORK: {
    // Allowed
    readToEOL(context);
    break;
  }

  case AllowToken::FILE_IOCTL_WRITE_XATTR:
  case AllowToken::FILE_READ_XATTR: {
    // In order to support this, the build system would need to include xattrs
    // in the build step dirtiness calculations.
    result.violations.emplace_back(
        "Process performed unsupported action " + token_slice.asString() +
        ". If this affects you, please report this to the project maintainers, "
        "this can be fixed.");
    readToEOL(context);
    break;
  }

  case AllowToken::SIGNAL:
  case AllowToken::NETWORK_STAR:
  case AllowToken::NETWORK_INBOUND:
  case AllowToken::NETWORK_BIND:
  case AllowToken::FILE_WRITE_UNMOUNT:
  case AllowToken::FILE_WRITE_MOUNT:
  case AllowToken::SYSCTL_STAR:
  case AllowToken::SYSCTL_WRITE:
  case AllowToken::SYSTEM_STAR:
  case AllowToken::SYSTEM_ACCT:
  case AllowToken::SYSTEM_AUDIT:
  case AllowToken::SYSTEM_FSCTL:
  case AllowToken::SYSTEM_LCID:
  case AllowToken::SYSTEM_MAC_LABEL:
  case AllowToken::SYSTEM_NFSSVC:
  case AllowToken::SYSTEM_REBOOT:
  case AllowToken::SYSTEM_SET_TIME:
  case AllowToken::SYSTEM_SOCKET:
  case AllowToken::SYSTEM_SWAP:
  case AllowToken::SYSTEM_WRITE_BOOTSTRAP:
  case AllowToken::JOB_CREATION:
  case AllowToken::IPC_STAR:
  case AllowToken::IPC_POSIX_STAR:
  case AllowToken::IPC_POSIX_SEM:
  case AllowToken::IPC_POSIX_SHM:
  case AllowToken::IPC_SYSV_STAR:
  case AllowToken::IPC_SYSV_MSG:
  case AllowToken::IPC_SYSV_SEM:
  case AllowToken::IPC_SYSV_SHM:
  case AllowToken::MACH_STAR:
  case AllowToken::MACH_PER_USER_LOOKUP:
  case AllowToken::MACH_BOOTSTRAP:
  case AllowToken::MACH_LOOKUP:
  case AllowToken::MACH_PRIV_STAR:
  case AllowToken::MACH_PRIV_HOST_PORT:
  case AllowToken::MACH_PRIV_TASK_PORT:
  case AllowToken::MACH_TASK_NAME: {
    result.violations.emplace_back(
        "Process performed disallowed action " + token_slice.asString());
    readToEOL(context);
    break;
  }
  }
}

void readLine(
    const SandboxIgnores &ignores,
    std::unordered_set<std::string> &must_create,
    ParsingContext &context,
    SandboxResult &result) throw(ParseError) {
  if (!readOpeningParen(context)) {
    return;
  }

  const auto token = readStatementToken(context);
  readWhitespace(context);

  switch (token) {
  case StatementToken::VERSION:
  case StatementToken::DENY:
    readToEOL(context);
    break;
  case StatementToken::ALLOW:
    readAllow(ignores, must_create, context, result);
    break;
  }
}

}  // anonymous namespace

SandboxResult parseSandbox(
    const SandboxIgnores &ignores,
    std::string &&contents) throw(ParseError) {
  SandboxResult result;
  ParsingContext context(&contents[0], contents.data() + contents.size());
  std::unordered_set<std::string> must_create;
  while (!context.atEnd()) {
    readLine(ignores, must_create, context, result);
  }


  for (const auto &path : must_create) {
    if (result.created.count(path) == 0) {
      result.violations.emplace_back(
          "Process unlinked file or directory that it did not create: " + path);
    }
  }
  return result;
}

}  // namespace shk
