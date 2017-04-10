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

  SECTION("Hash") {
    Step empty;

    auto a = Step::Builder()
        .setInputs({ paths.get("input") })
        .setImplicitInputs({ paths.get("implicit_input") })
        .setDependencies({ paths.get("dependencies_input") })
        .setOutputs({ paths.get("output") })
        .setPoolName("pool")
        .setCommand("cmd")
        .setDescription("desc")
        .setDepfile(Optional<Path>(paths.get("dep")))
        .setRspfile(Optional<Path>(paths.get("rsp")))
        .setRspfileContent("rsp_content")
        .build();

    SECTION("Basic") {
      CHECK(empty.hash() == empty.hash());
      CHECK(a.hash() == a.hash());
      CHECK(a.hash() != empty.hash());
    }

    SECTION("Bleed") {
      auto a = Step::Builder()
          .setDependencies({ paths.get("a") })
          .setOutputs({ paths.get("b") })
          .build();
      auto b = Step::Builder()
          .setDependencies({})
          .setOutputs({ paths.get("a"), paths.get("b") })
          .build();
      CHECK(a.hash() != b.hash());
    }

    SECTION("Input") {
      auto b = a.toBuilder()
          .setInputs({})
          .build();
      CHECK(a.hash() != b.hash());
    }

    SECTION("ImplicitInputs") {
      auto b = a.toBuilder()
          .setImplicitInputs({})
          .build();
      CHECK(a.hash() != b.hash());
    }

    SECTION("Dependencies") {
      auto b = a.toBuilder()
          .setDependencies({})
          .build();
      CHECK(a.hash() != b.hash());
    }

    SECTION("Outputs") {
      auto b = a.toBuilder()
          .setOutputs({})
          .build();
      CHECK(a.hash() != b.hash());
    }

    SECTION("PoolName") {
      // The pool is not significant for the build results
      auto b = a.toBuilder()
          .setPoolName("")
          .build();
      CHECK(a.hash() == b.hash());
    }

    SECTION("Command") {
      auto b = a.toBuilder()
          .setCommand("")
          .build();
      CHECK(a.hash() != b.hash());
    }

    SECTION("Description") {
      // The description is not significant for the build results
      auto b = a.toBuilder()
          .setDescription("")
          .build();
      CHECK(a.hash() == b.hash());
    }

    SECTION("Depfile") {
      // The depfile path is not significant for the build results
      auto b = a.toBuilder()
          .setDepfile(Optional<Path>(paths.get("other")))
          .build();
      CHECK(a.hash() == b.hash());
    }

    SECTION("Generator") {
      auto b = a.toBuilder()
          .setGenerator(!a.generator)
          .build();
      CHECK(a.hash() != b.hash());
    }

    SECTION("GeneratorCommandline") {
      // Generator rules don't include the command line when calculating
      // dirtiness
      auto one = a.toBuilder()
          .setGenerator(true)
          .build();
      auto two = one.toBuilder()
          .setCommand(a.command + "somethingelse")
          .build();
      CHECK(one.hash() == two.hash());
    }

    SECTION("Rspfile") {
      // The rspfile path is not significant for the build results
      auto b = a.toBuilder()
          .setRspfile(Optional<Path>(paths.get("other")))
          .build();
      CHECK(a.hash() == b.hash());
    }

    SECTION("RspfileContent") {
      auto b = a.toBuilder()
          .setRspfileContent("other")
          .build();
      CHECK(a.hash() != b.hash());
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
