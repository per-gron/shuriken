#include <catch.hpp>

#include "fs/path.h"
#include "manifest/step.h"

#include "../in_memory_file_system.h"

namespace shk {

TEST_CASE("Step") {
  InMemoryFileSystem fs;
  Paths paths(fs);

  SECTION("CopyConstructor") {
    auto a = Step::Builder()
        .setDependencies({ 0 })
        .build();

    auto b = a;
    CHECK(a.dependencies == b.dependencies);
  }

  SECTION("ToAndFromBuilder") {
    SECTION("Hash") {
      Hash other_hash{};
      other_hash.data[0] = 1;

      auto a = Step::Builder()
          .build();
      auto b = a.toBuilder()
          .setHash(std::move(other_hash))
          .build();
      CHECK(a.hash == Hash());
      CHECK(b.hash == other_hash);
    }

    SECTION("Dependencies") {
      auto a = Step::Builder()
          .setDependencies({ 0 })
          .build();
      auto b = a.toBuilder()
          .setDependencies({})
          .build();
      CHECK(a.dependencies == std::vector<StepIndex>{ 0 });
      CHECK(b.dependencies == std::vector<StepIndex>{});
    }

    SECTION("OutputDirs") {
      auto a = Step::Builder()
          .setOutputDirs({ "o1" })
          .build();
      auto b = a.toBuilder()
          .setOutputDirs({ "o2" })
          .build();
      CHECK(a.output_dirs == std::vector<std::string>{ "o1" });
      CHECK(b.output_dirs == std::vector<std::string>{ "o2" });
    }

    SECTION("PoolName") {
      auto a = Step::Builder()
          .setPoolName("a")
          .build();
      auto b = a.toBuilder()
          .setPoolName("b")
          .build();
      CHECK(a.pool_name == "a");
      CHECK(b.pool_name == "b");
    }

    SECTION("Command") {
      auto a = Step::Builder()
          .setCommand("a")
          .build();
      auto b = a.toBuilder()
          .setCommand("b")
          .build();
      CHECK(a.command == "a");
      CHECK(b.command == "b");
    }

    SECTION("Description") {
      auto a = Step::Builder()
          .setDescription("a")
          .build();
      auto b = a.toBuilder()
          .setDescription("b")
          .build();
      CHECK(a.description == "a");
      CHECK(b.description == "b");
    }

    SECTION("Depfile") {
      auto a = Step::Builder()
          .setDepfile("a")
          .build();
      auto b = a.toBuilder()
          .setDepfile("b")
          .build();
      CHECK(a.depfile == "a");
      CHECK(b.depfile == "b");
    }

    SECTION("Generator") {
      auto a = Step::Builder()
          .setGenerator(true)
          .build();
      auto b = a.toBuilder()
          .setGenerator(false)
          .build();
      CHECK(a.generator);
      CHECK(!b.generator);
    }

    SECTION("Rspfile") {
      auto a = Step::Builder()
          .setRspfile("a")
          .build();
      auto b = a.toBuilder()
          .setRspfile("b")
          .build();
      CHECK(a.rspfile == "a");
      CHECK(b.rspfile == "b");
    }

    SECTION("RspfileContent") {
      auto a = Step::Builder()
          .setRspfileContent("a")
          .build();
      auto b = a.toBuilder()
          .setRspfileContent("b")
          .build();
      CHECK(a.rspfile_content == "a");
      CHECK(b.rspfile_content == "b");
    }
  }

  SECTION("phony") {
    CHECK(!Step::Builder().setCommand("cmd").build().phony());
    CHECK(Step::Builder().build().phony());
    CHECK(Step::Builder().setCommand("").build().phony());
  }

  SECTION("isConsolePool") {
    CHECK(!isConsolePool(""));
    CHECK(!isConsolePool("a"));
    CHECK(!isConsolePool("consol"));
    CHECK(!isConsolePool("console_"));
    CHECK(isConsolePool("console"));
  }
}

}  // namespace shk
