#include <catch.hpp>

#include "fs/path.h"
#include "manifest/step.h"

#include "../in_memory_file_system.h"
#include "step_builder.h"

namespace shk {
namespace {

template <typename View>
std::vector<typename View::value_type> toVector(View view) {
  auto ans = std::vector<typename View::value_type>();
  ans.reserve(view.size());
  std::copy(view.begin(), view.end(), std::back_inserter(ans));
  return ans;
}

}  // anonymous namespace

TEST_CASE("Step") {
  InMemoryFileSystem fs;
  Paths paths(fs);

  SECTION("CopyConstructor") {
    auto a = StepBuilder()
        .setDependencies({ 0 })
        .build();

    auto b = a;
    CHECK(a.dependencies() == b.dependencies());
  }

  SECTION("ToAndFromBuilder") {
    SECTION("Hash") {
      Hash other_hash{};
      other_hash.data[0] = 1;

      auto a = StepBuilder()
          .build();
      auto b = StepBuilder::fromStep(a)
          .setHash(std::move(other_hash))
          .build();
      CHECK(a.hash() == Hash());
      CHECK(b.hash() == other_hash);
    }

    SECTION("Dependencies") {
      auto a = StepBuilder()
          .setDependencies({ 0 })
          .build();
      auto b = StepBuilder::fromStep(a)
          .setDependencies({})
          .build();
      REQUIRE(a.dependencies().size() == 1);
      CHECK(a.dependencies().front() == 0);
      CHECK(b.dependencies().empty());
    }

    SECTION("OutputDirs") {
      auto a = StepBuilder()
          .setOutputDirs({ "o1" })
          .build();
      auto b = StepBuilder::fromStep(a)
          .setOutputDirs({ "o2" })
          .build();
      CHECK(toVector(a.outputDirs()) == std::vector<nt_string_view>{ "o1" });
      CHECK(toVector(b.outputDirs()) == std::vector<nt_string_view>{ "o2" });
    }

    SECTION("PoolName") {
      auto a = StepBuilder()
          .setPoolName("a")
          .build();
      auto b = StepBuilder::fromStep(a)
          .setPoolName("b")
          .build();
      CHECK(a.poolName() == "a");
      CHECK(b.poolName() == "b");
    }

    SECTION("Command") {
      auto a = StepBuilder()
          .setCommand("a")
          .build();
      auto b = StepBuilder::fromStep(a)
          .setCommand("b")
          .build();
      CHECK(a.command() == "a");
      CHECK(b.command() == "b");
    }

    SECTION("Description") {
      auto a = StepBuilder()
          .setDescription("a")
          .build();
      auto b = StepBuilder::fromStep(a)
          .setDescription("b")
          .build();
      CHECK(a.description() == "a");
      CHECK(b.description() == "b");
    }

    SECTION("Depfile") {
      auto a = StepBuilder()
          .setDepfile("a")
          .build();
      auto b = StepBuilder::fromStep(a)
          .setDepfile("b")
          .build();
      CHECK(a.depfile() == "a");
      CHECK(b.depfile() == "b");
    }

    SECTION("Generator") {
      auto a = StepBuilder()
          .setGenerator(true)
          .build();
      auto b = StepBuilder::fromStep(a)
          .setGenerator(false)
          .build();
      CHECK(a.generator());
      CHECK(!b.generator());
    }

    SECTION("Rspfile") {
      auto a = StepBuilder()
          .setRspfile("a")
          .build();
      auto b = StepBuilder::fromStep(a)
          .setRspfile("b")
          .build();
      CHECK(a.rspfile() == "a");
      CHECK(b.rspfile() == "b");
    }

    SECTION("RspfileContent") {
      auto a = StepBuilder()
          .setRspfileContent("a")
          .build();
      auto b = StepBuilder::fromStep(a)
          .setRspfileContent("b")
          .build();
      CHECK(a.rspfileContent() == "a");
      CHECK(b.rspfileContent() == "b");
    }
  }

  SECTION("phony") {
    CHECK(!StepBuilder().setCommand("cmd").build().phony());
    CHECK(StepBuilder().build().phony());
    CHECK(StepBuilder().setCommand("").build().phony());
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
