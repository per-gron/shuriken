#include <catch.hpp>

#include "fs/path.h"
#include "sandbox_parser.h"

namespace shk {
namespace {

/**
 * Verify that a string parses with a single violation.
 */
SandboxResult checkDisallowedAllowFiles(
    const SandboxIgnores &ignores,
    std::string &&input,
    const std::string &violation) {
  const auto result = parseSandbox(ignores, std::move(input));
  REQUIRE(result.violations.size() == 1);
  CHECK(result.violations[0] == violation);
  return result;
}

/**
 * Verify that a string parses with a single violation.
 */
void checkDisallowed(
    const SandboxIgnores &ignores,
    std::string &&input,
    const std::string &violation) {
  const auto result = checkDisallowedAllowFiles(
      ignores, std::move(input), violation);
  CHECK(result.created.empty());
  CHECK(result.read.empty());
}

/**
 * Verify that a string parses with a single violation.
 */
void checkDisallowed(
    std::string &&input,
    const std::string &violation) {
  checkDisallowed(SandboxIgnores(), std::move(input), violation);
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
void checkEmpty(const SandboxIgnores &ignores, std::string &&input) {
  CHECK(parseSandbox(
      ignores, std::move(input)) == SandboxResult());
}

/**
 * Verify that a string parses with an empty result.
 */
void checkEmpty(std::string &&input) {
  checkEmpty(SandboxIgnores(), std::move(input));
}

/**
 * Verify that a string fails to parse.
 */
void checkFailsParse(std::string &&input) {
  CHECK_THROWS_AS(
      parseSandbox(SandboxIgnores(), std::move(input)), ParseError);
}

void compareOutputPaths(
    const std::vector<std::string> &a,
    const std::unordered_set<std::string> &b) {
  std::unordered_set<std::string> a_set;
  for (const auto &path : a) {
    a_set.insert(path);
  }
  CHECK(a_set == b);
}

void compareInputPaths(
    const std::vector<std::pair<std::string, DependencyType>> &a,
    const std::unordered_map<std::string, DependencyType> &b) {
  std::unordered_map<std::string, DependencyType> a_map;
  for (const auto &dep : a) {
    a_map.insert(dep);
  }
  CHECK(a_map == b);
}

void checkResult(
    const SandboxIgnores &ignores,
    std::string &&input,
    const std::vector<std::string> &created,
    const std::vector<std::pair<std::string, DependencyType>> &read) {
  const auto result = parseSandbox(ignores, std::move(input));
  compareOutputPaths(created, result.created);
  compareInputPaths(read, result.read);
  CHECK(result.violations.empty());
}

void checkResult(
    std::string &&input,
    const std::vector<std::string> &created,
    const std::vector<std::pair<std::string, DependencyType>> &read) {
  checkResult(SandboxIgnores(), std::move(input), created, read);
}

}  // anomymous namespace

TEST_CASE("SandboxParser") {
  SandboxIgnores network;
  network.network_access = { "/an/ignored/path" };

  SandboxIgnores file;
  file.file_access = { "/an/ignored/path" };

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
        "(allow file-read-data (path \"/a/path\"))",
        {},
        { { "/a/path", DependencyType::ALWAYS } });
    checkResult(
        "(allow file-read-xattr (path \"/a/path\"))",
        {},
        { { "/a/path", DependencyType::ALWAYS } });
    // literal is old syntax
    checkResult(
        "(allow file-read-data (literal \"/a/path\"))",
        {},
        { { "/a/path", DependencyType::ALWAYS } });
    checkResult(
        "(allow file-read-metadata (path \"/another/path\"))",
        {},
        { { "/another/path", DependencyType::IGNORE_IF_DIRECTORY } });
    checkResult(
        "(allow file-read-metadata (path \"/a/path\"))\n"
        "(allow file-read-data (path \"/a/path\"))",
        {},
        { { "/a/path", DependencyType::ALWAYS } });
    checkResult(
        "(allow file-read-data (path \"/a/path\"))\n"
        "(allow file-read-metadata (path \"/a/path\"))",
        {},
        { { "/a/path", DependencyType::ALWAYS } });
    checkResult(
        "(allow process-exec* (path \"/bin/ls\"))",
        {},
        { { "/bin/ls", DependencyType::ALWAYS } });
    checkResult(
        "(allow process-exec (path \"/bin/ls\"))",
        {},
        { { "/bin/ls", DependencyType::ALWAYS } });
    checkResult(
        "(allow process* (path \"/bin/ls\"))",
        {},
        { { "/bin/ls", DependencyType::ALWAYS } });

    // Ideally this should be disallowed
    checkResult(
        "(allow file-read-data (path \"/a/path\"))\n"
        "(allow file-write-create (path \"/a/path\"))\n",
        { "/a/path" },
        {});
  }

  SECTION("ReadIgnored") {
    checkEmpty(
        file,
        "(allow file-read-data (path \"/an/ignored/path\"))");
    checkEmpty(
        file,
        "(allow file-read-metadata (path \"/an/ignored/path\"))");
    checkEmpty(
        file,
        "(allow process-exec* (path \"/an/ignored/path\"))");
    checkEmpty(
        file,
        "(allow process-exec (path \"/an/ignored/path\"))");
    checkEmpty(
        file,
        "(allow process* (path \"/an/ignored/path\"))");
    checkEmpty(
        file,
        "(allow file-read-data (path \"/an/ignored/path\"))\n"
        "(allow file-write-create (path \"/an/ignored/path\"))\n");
  }

  SECTION("WriteWithoutCreate") {
    // Ideally these should all be disallowed but the sandbox tracing
    // mechanism cannot distinguish between opening a file for wite with
    // appending vs without append, so unfortunately it has to be allowed.
    checkResult(
        "(allow file-write-data (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-flags (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-mode (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-owner (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-setugid (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-times (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-revoke (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-xattr (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-ioctl-write-xattr (path \"/a/path\"))\n",
        { "/a/path" },
        {});
  }

  SECTION("WriteWithoutCreateIgnored") {
    checkEmpty(
        file,
        "(allow file-write-data (path \"/an/ignored/path\"))");
    checkEmpty(
        file,
        "(allow file-write-flags (path \"/an/ignored/path\"))");
    checkEmpty(
        file,
        "(allow file-write-mode (path \"/an/ignored/path\"))");
    checkEmpty(
        file,
        "(allow file-write-owner (path \"/an/ignored/path\"))");
    checkEmpty(
        file,
        "(allow file-write-setugid (path \"/an/ignored/path\"))");
    checkEmpty(
        file,
        "(allow file-write-times (path \"/an/ignored/path\"))");
    checkEmpty(
        file,
        "(allow file-revoke (path \"/an/ignored/path\"))");
  }

  SECTION("Unlink") {
    checkDisallowed(
        "(allow file-write-unlink (path \"/a/path\"))",
        "Process unlinked file or directory that it did not create: "
        "/a/path");

    checkEmpty(
        "(allow file-write-create (path \"/a/path\"))\n"
        "(allow file-write-unlink (path \"/a/path\"))\n");

    checkDisallowed(
        "(allow file-write-create (path \"/a/path\"))\n"
        "(allow file-write-unlink (path \"/a/path\"))\n"
        "(allow file-write-unlink (path \"/a/path\"))\n",
        "Process unlinked file or directory that it did not create: "
        "/a/path");

    checkResult(
        "(allow file-write-create (path \"/a/path\"))\n"
        "(allow file-write-unlink (path \"/a/path\"))\n"
        "(allow file-read-data (path \"/a/path\"))\n",
        {},
        { { "/a/path", DependencyType::ALWAYS } });
  }

  SECTION("UnlinkIgnored") {
    checkEmpty(
        file,
        "(allow file-write-unlink (path \"/an/ignored/path\"))");

    checkEmpty(
        file,
        "(allow file-write-create (path \"/an/ignored/path\"))\n"
        "(allow file-write-unlink (path \"/an/ignored/path\"))\n");

    checkEmpty(
        file,
        "(allow file-write-create (path \"/an/ignored/path\"))\n"
        "(allow file-write-unlink (path \"/an/ignored/path\"))\n"
        "(allow file-write-unlink (path \"/an/ignored/path\"))\n");

    checkEmpty(
        file,
        "(allow file-write-create (path \"/an/ignored/path\"))\n"
        "(allow file-write-unlink (path \"/an/ignored/path\"))\n"
        "(allow file-read-data (path \"/an/ignored/path\"))\n");
  }

  SECTION("Write") {
    checkResult(
        "(allow file-write-create (path \"/a/path\"))\n"
        "(allow file-write-data (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-create (path \"/a/path\"))\n"
        "(allow file-write-flags (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-create (path \"/a/path\"))\n"
        "(allow file-write-mode (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-create (path \"/a/path\"))\n"
        "(allow file-write-owner (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-create (path \"/a/path\"))\n"
        "(allow file-write-setugid (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-create (path \"/a/path\"))\n"
        "(allow file-write-times (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-create (path \"/a/path\"))\n"
        "(allow file-revoke (path \"/a/path\"))\n",
        { "/a/path" },
        {});
    checkResult(
        "(allow file-write-create (path \"/a/path\"))\n"
        "(allow file-read-data (path \"/a/path\"))\n",
        { "/a/path" },
        {});
  }

  SECTION("WriteIgnored") {
    checkEmpty(
        file,
        "(allow file-write-create (path \"/an/ignored/path\"))\n"
        "(allow file-write-data (path \"/an/ignored/path\"))\n");
    checkEmpty(
        file,
        "(allow file-write-create (path \"/an/ignored/path\"))\n"
        "(allow file-write-flags (path \"/an/ignored/path\"))\n");
    checkEmpty(
        file,
        "(allow file-write-create (path \"/an/ignored/path\"))\n"
        "(allow file-write-mode (path \"/an/ignored/path\"))\n");
    checkEmpty(
        file,
        "(allow file-write-create (path \"/an/ignored/path\"))\n"
        "(allow file-write-owner (path \"/an/ignored/path\"))\n");
    checkEmpty(
        file,
        "(allow file-write-create (path \"/an/ignored/path\"))\n"
        "(allow file-write-setugid (path \"/an/ignored/path\"))\n");
    checkEmpty(
        file,
        "(allow file-write-create (path \"/an/ignored/path\"))\n"
        "(allow file-revoke (path \"/an/ignored/path\"))\n");
    checkEmpty(
        file,
        "(allow file-write-create (path \"/an/ignored/path\"))\n"
        "(allow file-read-data (path \"/an/ignored/path\"))\n");
  }

  SECTION("LiteralEscaping") {
    checkResult(
        "(allow file-read-data (path \"/a\\\"b\"))",
        {},
        { { "/a\"b", DependencyType::ALWAYS } });
    checkResult(
        "(allow file-read-data (path \"/a\\nb\"))",
        {},
        { { "/a\nb", DependencyType::ALWAYS } });
    checkResult(
        "(allow file-read-data (path \"/a\\rb\"))",
        {},
        { { "/a\rb", DependencyType::ALWAYS } });
    checkResult(
        "(allow file-read-data (path \"/a\\tb\"))",
        {},
        { { "/a\tb", DependencyType::ALWAYS } });
    checkResult(
        "(allow file-read-data (path \"/a\\x22b\"))",
        {},
        { { "/a\"b", DependencyType::ALWAYS } });
    checkResult(
        "(allow file-read-data (path \"/a\\1b\"))",
        {},
        { { "/a\1b", DependencyType::ALWAYS } });
    checkResult(
        "(allow file-read-data (path \"/a\\01b\"))",
        {},
        { { "/a\1b", DependencyType::ALWAYS } });
    checkResult(
        "(allow file-read-data (path \"/a\\42b\"))",
        {},
        { { "/a\"b", DependencyType::ALWAYS } });

    checkFailsParse("(allow file-read-data (path \"\\a\"))");
  }

  SECTION("PartiallyDisallowed") {
    checkDisallowedAction("network-outbound", "(allow network-outbound (remote tcp4 \"*:80\"))");
    checkDisallowed(
        "(allow network-outbound (path \"/a/b\"))",
        "Process opened network connection on illegal path /a/b");

    checkEmpty(
        network, "(allow network-outbound (path \"/an/ignored/path\")");
    checkDisallowed(
        "(allow file-ioctl (path \"/an/ignored/path\"))",
        "Process used ioctl on illegal path /an/ignored/path");

    checkDisallowed(
        "(allow file-ioctl (path \"/a/b\"))",
        "Process used ioctl on illegal path /a/b");

    checkEmpty(file, "(allow file-ioctl (path \"/an/ignored/path\")");
    checkDisallowed(
        "(allow network-outbound (path \"/an/ignored/path\"))",
        "Process opened network connection on illegal path /an/ignored/path");
  }

  SECTION("Allowed") {
    checkEmpty(file, "(allow ipc*)");
    checkEmpty(file, "(allow ipc-posix*)");
    checkEmpty(file, "(allow ipc-posix-sem)");
    checkEmpty(file, "(allow ipc-posix-shm)");
    checkEmpty(file, "(allow ipc-sysv*)");
    checkEmpty(file, "(allow ipc-sysv-msg)");
    checkEmpty(file, "(allow ipc-sysv-sem)");
    checkEmpty(file, "(allow ipc-sysv-shm)");
    checkEmpty(file, "(allow mach*)");
    checkEmpty(file, "(allow mach-per-user-lookup)");
    checkEmpty(file, "(allow mach-bootstrap)");
    checkEmpty(file, "(allow mach-lookup)");
    checkEmpty(file, "(allow mach-priv*)");
    checkEmpty(file, "(allow mach-priv-host-port)");
    checkEmpty(file, "(allow mach-priv-task-port)");
    checkEmpty(file, "(allow mach-task-name)");
}

  SECTION("Disallowed") {
    checkDisallowedAction("signal", "(allow signal)");
    checkDisallowedAction("network*", "(allow network*)");
    checkDisallowedAction("network-inbound", "(allow network-inbound)");
    checkDisallowedAction("network-bind", "(allow network-bind)");
    checkDisallowedAction("file-write-unmount", "(allow file-write-unmount)");
    checkDisallowedAction("file-write-mount", "(allow file-write-mount)");
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
