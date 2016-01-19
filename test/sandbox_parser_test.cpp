#include <catch.hpp>

#include "path.h"
#include "sandbox_parser.h"

namespace shk {
namespace {

/**
 * Verify that a string parses with a single violation.
 */
SandboxResult checkDisallowedAllowFiles(
    std::string &&input,
    const std::string &violation) {
  Paths paths;
  const auto result = parseSandbox(paths, std::move(input));
  REQUIRE(result.violations.size() == 1);
  CHECK(result.violations[0] == violation);
  return result;
}

/**
 * Verify that a string parses with a single violation.
 */
void checkDisallowed(
    std::string &&input,
    const std::string &violation) {
  const auto result = checkDisallowedAllowFiles(std::move(input), violation);
  CHECK(result.created.empty());
  CHECK(result.read.empty());
}

/**
 * Verify that a string parses with a single violation claiming that the
 * specified action is disallowed.
 */
void checkDisallowedAction(const std::string &action, std::string &&input) {
  checkDisallowed(
      std::move(input),
      "Process performed disallowed action " + action);
}

/**
 * Verify that a string parses with an empty result.
 */
void checkEmpty(std::string &&input) {
  Paths paths;
  CHECK(parseSandbox(paths, std::move(input)) == SandboxResult());
}

/**
 * Verify that a string fails to parse.
 */
void checkFailsParse(std::string &&input) {
  Paths paths;
  CHECK_THROWS_AS(parseSandbox(paths, std::move(input)), ParseError);
}

void comparePaths(
    Paths &paths,
    const std::vector<std::string> &a,
    const std::unordered_set<Path> &b) {
  std::unordered_set<Path> a_set;
  for (const auto &path : a) {
    a_set.insert(paths.get(path));
  }
  CHECK(a_set == b);
}

void checkResult(
    std::string &&input,
    const std::vector<std::string> &created,
    const std::vector<std::string> &read) {
  Paths paths;
  const auto result = parseSandbox(paths, std::move(input));
  comparePaths(paths, created, result.created);
  comparePaths(paths, read, result.read);
  CHECK(result.violations.empty());
}

}  // anomymous namespace

TEST_CASE("SandboxParser") {
  Paths paths;

  SECTION("EmptyAndComments") {
    checkEmpty("");
    checkEmpty(" ");
    checkEmpty(";");
    checkEmpty("; hello");
    checkEmpty("; (");
    checkEmpty(" ;");
    checkEmpty(" ; x");
    checkEmpty("\n;");
    checkEmpty(";\n");
  }

  SECTION("Version") {
    checkEmpty("(version 1)");
    checkEmpty("(version 1)(version 1)");
    checkEmpty("(version 1)(version 1)(version 1)");
    checkEmpty(" (version 1)");
    checkEmpty(" (version 1) ");
    checkEmpty("\n(version 1)\n");
  }

  SECTION("Read") {
    checkResult(
        "(allow file-read-data (literal \"/a/path\"))",
        {},
        { "/a/path" });
    checkResult(
        "(allow file-read-metadata (literal \"/another/path\"))",
        {},
        { "/another/path" });
    checkDisallowedAllowFiles(
        "(allow file-read-data (literal \"/a/path\"))\n"
        "(allow file-write-create (literal \"/a/path\"))\n",
        "Process created file that it had previously read from: /a/path");
  }

  SECTION("WriteWithoutCreate") {
    checkDisallowed(
        "(allow file-write-data (literal \"/a/path\"))",
        "Process performed action file-write-data on file or directory that "
        "it did not create: /a/path");
    checkDisallowed(
        "(allow file-write-flags (literal \"/a/path\"))",
        "Process performed action file-write-flags on file or directory that"
        " it did not create: /a/path");
    checkDisallowed(
        "(allow file-write-mode (literal \"/a/path\"))",
        "Process performed action file-write-mode on file or directory that "
        "it did not create: /a/path");
    checkDisallowed(
        "(allow file-write-owner (literal \"/a/path\"))",
        "Process performed action file-write-owner on file or directory that"
        " it did not create: /a/path");
    checkDisallowed(
        "(allow file-write-setugid (literal \"/a/path\"))",
        "Process performed action file-write-setugid on file or directory "
        "that it did not create: /a/path");
    checkDisallowed(
        "(allow file-revoke (literal \"/a/path\"))",
        "Process performed action file-revoke on file or directory that it "
        "did not create: /a/path");
  }

  SECTION("Unlink") {
    checkDisallowed(
        "(allow file-write-unlink (literal \"/a/path\"))",
        "Process unlinked file or directory that it did not create: "
        "/a/path");

    checkEmpty(
        "(allow file-write-create (literal \"/a/path\"))\n"
        "(allow file-write-unlink (literal \"/a/path\"))\n");

    checkDisallowed(
        "(allow file-write-create (literal \"/a/path\"))\n"
        "(allow file-write-unlink (literal \"/a/path\"))\n"
        "(allow file-write-unlink (literal \"/a/path\"))\n",
        "Process unlinked file or directory that it did not create: "
        "/a/path");

    checkResult(
        "(allow file-write-create (literal \"/a/path\"))\n"
        "(allow file-write-unlink (literal \"/a/path\"))\n"
        "(allow file-read-data (literal \"/a/path\"))\n",
        {},
        { "/a/path" });
  }

  SECTION("Write") {
    checkResult(
        "(allow file-write-create (literal \"/a/path\"))\n"
        "(allow file-write-data (literal \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-create (literal \"/a/path\"))\n"
        "(allow file-write-flags (literal \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-create (literal \"/a/path\"))\n"
        "(allow file-write-mode (literal \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-create (literal \"/a/path\"))\n"
        "(allow file-write-owner (literal \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-create (literal \"/a/path\"))\n"
        "(allow file-write-setugid (literal \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-create (literal \"/a/path\"))\n"
        "(allow file-revoke (literal \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-create (literal \"/a/path\"))\n"
        "(allow file-read-data (literal \"/a/path\"))\n",
        { "/a/path" },
        {});
  }

  SECTION("LiteralEscaping") {
    checkResult(
        "(allow file-read-data (literal \"/a\\\"b\"))",
        {},
        { "/a\"b" });
    checkResult(
        "(allow file-read-data (literal \"/a\\nb\"))",
        {},
        { "/a\nb" });
    checkResult(
        "(allow file-read-data (literal \"/a\\rb\"))",
        {},
        { "/a\rb" });
    checkResult(
        "(allow file-read-data (literal \"/a\\tb\"))",
        {},
        { "/a\tb" });
    checkResult(
        "(allow file-read-data (literal \"/a\\x22b\"))",
        {},
        { "/a\"b" });
    checkResult(
        "(allow file-read-data (literal \"/a\\1b\"))",
        {},
        { "/a\1b" });
    checkResult(
        "(allow file-read-data (literal \"/a\\01b\"))",
        {},
        { "/a\1b" });
    checkResult(
        "(allow file-read-data (literal \"/a\\42b\"))",
        {},
        { "/a\"b" });

    checkFailsParse("(allow file-read-data (literal \"\\a\"))");
  }

  SECTION("PartiallyDisallowed") {
    checkDisallowedAction("network-outbound", "(allow network-outbound (remote tcp4 \"*:80\"))");
    checkDisallowed(
        "(allow network-outbound (literal \"/a/b\"))",
        "Process opened network connection on illegal path /a/b");
    checkEmpty("(allow network-outbound (literal \"/private/var/run/syslog\")");

    checkDisallowed(
        "(allow file-ioctl (literal \"/a/b\"))",
        "Process used ioctl on illegal path /a/b");
    checkEmpty("(allow file-ioctl (literal \"/dev/dtracehelper\")");
  }

  SECTION("Disallowed") {
    checkDisallowedAction("signal", "(allow signal)");
    checkDisallowedAction("network*", "(allow network*)");
    checkDisallowedAction("network-inbound", "(allow network-inbound)");
    checkDisallowedAction("network-bind", "(allow network-bind)");
    checkDisallowedAction("file-write-unmount", "(allow file-write-unmount)");
    checkDisallowedAction("file-write-mount", "(allow file-write-mount)");
    checkDisallowedAction("file-write-times", "(allow file-write-times)");
    checkDisallowedAction("sysctl*", "(allow sysctl*)");
    checkDisallowedAction("sysctl-write", "(allow sysctl-write)");
    checkDisallowedAction("system*", "(allow system*)");
    checkDisallowedAction("system-acct", "(allow system-acct)");
    checkDisallowedAction("system-audit", "(allow system-audit)");
    checkDisallowedAction("system-fsctl", "(allow system-fsctl)");
    checkDisallowedAction("system-lcid", "(allow system-lcid)");
    checkDisallowedAction("system-mac-label", "(allow system-mac-label)");
    checkDisallowedAction("system-nfssvc", "(allow system-nfssvc)");
    checkDisallowedAction("system-reboot", "(allow system-reboot)");
    checkDisallowedAction("system-set-time", "(allow system-set-time)");
    checkDisallowedAction("system-socket", "(allow system-socket)");
    checkDisallowedAction("system-swap", "(allow system-swap)");
    checkDisallowedAction("system-write-bootstrap", "(allow system-write-bootstrap)");
    checkDisallowedAction("job-creation", "(allow job-creation)");
    checkDisallowedAction("ipc*", "(allow ipc*)");
    checkDisallowedAction("ipc-posix*", "(allow ipc-posix*)");
    checkDisallowedAction("ipc-posix-sem", "(allow ipc-posix-sem)");
    checkDisallowedAction("ipc-posix-shm", "(allow ipc-posix-shm)");
    checkDisallowedAction("ipc-sysv*", "(allow ipc-sysv*)");
    checkDisallowedAction("ipc-sysv-msg", "(allow ipc-sysv-msg)");
    checkDisallowedAction("ipc-sysv-sem", "(allow ipc-sysv-sem)");
    checkDisallowedAction("ipc-sysv-shm", "(allow ipc-sysv-shm)");
    checkDisallowedAction("mach*", "(allow mach*)");
    checkDisallowedAction("mach-per-user-lookup", "(allow mach-per-user-lookup)");
    checkDisallowedAction("mach-bootstrap", "(allow mach-bootstrap)");
    checkDisallowedAction("mach-lookup", "(allow mach-lookup)");
    checkDisallowedAction("mach-priv*", "(allow mach-priv*)");
    checkDisallowedAction("mach-priv-host-port", "(allow mach-priv-host-port)");
    checkDisallowedAction("mach-priv-task-port", "(allow mach-priv-task-port)");
    checkDisallowedAction("mach-task-name", "(allow mach-task-name)");
  }

  SECTION("InvalidSyntax") {
    checkFailsParse("hej");
    checkFailsParse("(");
    checkFailsParse("(allowsignal)");
    checkFailsParse("(allow unknown)");
    checkFailsParse(";\n(");
    checkFailsParse("()");
    checkFailsParse("(a)");
    checkFailsParse("(allow)");
  }
}

}  // namespace shk
