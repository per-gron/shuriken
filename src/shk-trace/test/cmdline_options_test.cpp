#include <catch.hpp>

#include "cmdline_options.h"

namespace shk {
namespace {

CmdlineOptions parse(const std::vector<std::string> &options) {
  std::vector<char *> opts_cstr{ const_cast<char *>("trace") };
  for (const auto &option : options) {
    opts_cstr.push_back(const_cast<char *>(option.c_str()));
  }
  return CmdlineOptions::parse(opts_cstr.size(), opts_cstr.data());
}

}  // anonymous namespace

TEST_CASE("CmdlineOptions") {
  SECTION("Version") {
    CHECK(parse({ "--version" }).result == CmdlineOptions::Result::VERSION);
  }

  SECTION("Help") {
    CHECK(parse({ "--help" }).result == CmdlineOptions::Result::HELP);
    CHECK(parse({ "-h" }).result == CmdlineOptions::Result::HELP);
  }

  SECTION("Empty") {
    CHECK(parse({}).result == CmdlineOptions::Result::HELP);
  }

  SECTION("Nonflag") {
    CHECK(parse({ "xyz" }).result == CmdlineOptions::Result::HELP);
  }

  SECTION("Trailing") {
    CHECK(parse({ "-f", "file", "-c", "cmd", "xyz" }).result == CmdlineOptions::Result::HELP);
    CHECK(parse({ "-f", "file", "xyz", "-c", "cmd" }).result == CmdlineOptions::Result::HELP);
  }

  SECTION("JustCommand") {
    auto options = parse({ "-c", "abc" });
    CHECK(options.result == CmdlineOptions::Result::SUCCESS);
    CHECK(options.tracefile == "/dev/null");
    CHECK(options.command == "abc");
    CHECK(options.suicide_when_orphaned == false);
  }

  SECTION("SuicideWhenOrphaned") {
    auto options = parse({ "-c", "abc", "--suicide-when-orphaned" });
    CHECK(options.result == CmdlineOptions::Result::SUCCESS);
    CHECK(options.tracefile == "/dev/null");
    CHECK(options.command == "abc");
    CHECK(options.suicide_when_orphaned == true);
  }

  SECTION("SuicideWhenOrphanedShort") {
    auto options = parse({ "-c", "abc", "-O" });
    CHECK(options.result == CmdlineOptions::Result::SUCCESS);
    CHECK(options.tracefile == "/dev/null");
    CHECK(options.command == "abc");
    CHECK(options.suicide_when_orphaned == true);
  }

  SECTION("JustTracefile") {
    CHECK(parse({ "-f", "xyz" }).result == CmdlineOptions::Result::HELP);
  }

  SECTION("MissingFollowup") {
    CHECK(parse({ "-f" }).result == CmdlineOptions::Result::HELP);
    CHECK(parse({ "-f", "file", "-c" }).result == CmdlineOptions::Result::HELP);
    CHECK(parse({ "-c", "cmd", "-f" }).result == CmdlineOptions::Result::HELP);
  }

  SECTION("TwoTracefiles") {
    CHECK(
        parse({ "-c", "abc", "-f", "xyz", "-f", "123" }).result ==
        CmdlineOptions::Result::HELP);
  }

  SECTION("EmptyTraceFile") {
    CHECK(
        parse({ "-c", "abc", "-f", "" }).result ==
        CmdlineOptions::Result::HELP);
  }

  SECTION("TwoCommands") {
    CHECK(
        parse({ "-c", "abc", "-c", "xyz", "-f", "123" }).result ==
        CmdlineOptions::Result::HELP);
  }

  SECTION("CommandFirst") {
    auto options = parse({ "-c", "abc", "-f", "123" });
    CHECK(options.tracefile == "123");
    CHECK(options.command == "abc");
  }

  SECTION("TracefileFirst") {
    auto options = parse({ "-f", "abc", "-c", "123" });
    CHECK(options.tracefile == "abc");
    CHECK(options.command == "123");
  }
}

}  // namespace shk
