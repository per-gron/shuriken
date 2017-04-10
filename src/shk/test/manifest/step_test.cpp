#include <catch.hpp>

#include "fs/path.h"
#include "manifest/step.h"

#include "../in_memory_file_system.h"

namespace shk {

TEST_CASE("Step") {
  InMemoryFileSystem fs;
  Paths paths(fs);

  SECTION("Construct") {
    auto a = Step::Builder()
        .setInputs({ paths.get("input") })
        .build();

    SECTION("CopyConstructor") {
      auto b = a;
      CHECK(a.inputs == b.inputs);
    }
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

    SECTION("Inputs") {
      auto a = Step::Builder()
          .setInputs({ paths.get("input") })
          .build();
      auto b = a.toBuilder()
          .setInputs({})
          .build();
      CHECK(a.inputs == std::vector<Path>{ paths.get("input") });
      CHECK(b.inputs == std::vector<Path>{});
    }

    SECTION("ImplicitInputs") {
      auto a = Step::Builder()
          .setImplicitInputs({ paths.get("input") })
          .build();
      auto b = a.toBuilder()
          .setImplicitInputs({})
          .build();
      CHECK(a.implicit_inputs == std::vector<Path>{ paths.get("input") });
      CHECK(b.implicit_inputs == std::vector<Path>{});
    }

    SECTION("Dependencies") {
      auto a = Step::Builder()
          .setDependencies({ paths.get("input") })
          .build();
      auto b = a.toBuilder()
          .setDependencies({})
          .build();
      CHECK(a.dependencies == std::vector<Path>{ paths.get("input") });
      CHECK(b.dependencies == std::vector<Path>{});
    }

    SECTION("Outputs") {
      auto a = Step::Builder()
          .setOutputs({ paths.get("input") })
          .build();
      auto b = a.toBuilder()
          .setOutputs({})
          .build();
      CHECK(a.outputs == std::vector<Path>{ paths.get("input") });
      CHECK(b.outputs == std::vector<Path>{});
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
          .setDepfile(Optional<Path>(paths.get("a")))
          .build();
      auto b = a.toBuilder()
          .setDepfile(Optional<Path>(paths.get("b")))
          .build();
      CHECK(a.depfile == paths.get("a"));
      CHECK(b.depfile == paths.get("b"));
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
          .setRspfile(Optional<Path>(paths.get("a")))
          .build();
      auto b = a.toBuilder()
          .setRspfile(Optional<Path>(paths.get("b")))
          .build();
      CHECK(a.rspfile == paths.get("a"));
      CHECK(b.rspfile == paths.get("b"));
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

  SECTION("isConsolePool") {
    CHECK(!isConsolePool(""));
    CHECK(!isConsolePool("a"));
    CHECK(!isConsolePool("consol"));
    CHECK(!isConsolePool("console_"));
    CHECK(isConsolePool("console"));
  }
}

}  // namespace shk
