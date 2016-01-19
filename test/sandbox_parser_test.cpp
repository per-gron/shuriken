#include <catch.hpp>

#include "path.h"
#include "sandbox_parser.h"

namespace shk {
namespace {

/**
 * Verify that a string parses with a single violation.
 */
void checkDisallowed(
    std::string &&input,
    const std::string &violation) {
  Paths paths;
  const auto result = parseSandbox(paths, std::move(input));
  CHECK(result.created.empty());
  CHECK(result.read.empty());
  REQUIRE(result.violations.size() == 1);
  CHECK(result.violations[0] == violation);
}

/**
 * Verify that a string parses with a single violation claiming that the
 * specified action is disallowed.
 */
void checkDisallowedAction(const std::string &action, std::string &&input) {
  checkDisallowed(
      std::move(input),
      "Subprocess performed disallowed action " + action);
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
    // TODO(peck): Fill me in
  }

  SECTION("Create") {
    // TODO(peck): Fill me in
  }

  SECTION("PartiallyDisallowed") {
    checkDisallowedAction("network-outbound", "(allow network-outbound (remote tcp4 \"*:80\"))");
    checkDisallowed(
        "(allow network-outbound (literal \"/a/b\"))",
        "Subprocess opened network connection on illegal path /a/b");
    checkEmpty("(allow network-outbound (literal \"/private/var/run/syslog\")");

    checkDisallowed(
        "(allow file-ioctl (literal \"/a/b\"))",
        "Subprocess used ioctl on illegal path /a/b");
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
