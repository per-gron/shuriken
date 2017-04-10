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

    SECTION("RawStepMoveConversionConstructor") {
      RawStep a;
      a.inputs.push_back(paths.get("input"));
      a.implicit_inputs.push_back(paths.get("implicit_input"));
      a.dependencies.push_back(paths.get("dependencies_input"));
      a.outputs.push_back(paths.get("output"));
      a.pool_name = "pool";
      a.command = "cmd";
      a.description = "desc";
      a.depfile = paths.get("dep");
      a.rspfile = paths.get("rsp");
      a.rspfile_content = "rsp_content";

      auto b = Step(RawStep(a));

      CHECK(a.inputs == b.inputs);
      CHECK(a.implicit_inputs == b.implicit_inputs);
      CHECK(a.dependencies == b.dependencies);
      CHECK(a.outputs == b.outputs);
      CHECK(a.pool_name == b.pool_name);
      CHECK(a.command == b.command);
      CHECK(a.description == b.description);
      CHECK(a.depfile == b.depfile);
      CHECK(a.rspfile == b.rspfile);
      CHECK(a.rspfile_content == b.rspfile_content);
    }

    SECTION("CopyConstructor") {
      auto b = a;
      CHECK(a.inputs == b.inputs);
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
