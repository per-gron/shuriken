#include <catch.hpp>

#include "fs/path.h"
#include "manifest/raw_step.h"

#include "../in_memory_file_system.h"

namespace shk {

TEST_CASE("RawStep") {
  SECTION("Hash") {
    InMemoryFileSystem fs;
    Paths paths(fs);

    RawStep empty;

    RawStep a;
    a.inputs.push_back(paths.get("input"));
    a.implicit_inputs.push_back(paths.get("implicit_input"));
    a.dependencies.push_back(paths.get("dependencies_input"));
    a.outputs.push_back(paths.get("output"));
    a.pool_name = "pool";
    a.command = "cmd";
    a.description = "desc";
    a.depfile = paths.get("dep");
    a.rspfile = "rsp";
    a.rspfile_content = "rsp_content";

    RawStep b = a;

    SECTION("Basic") {
      CHECK(empty.hash() == empty.hash());
      CHECK(a.hash() == a.hash());
      CHECK(a.hash() != empty.hash());
    }

    SECTION("Bleed") {
      a.dependencies = { paths.get("a") };
      a.outputs = { paths.get("b") };
      b.dependencies = {};
      b.outputs = { paths.get("a"), paths.get("b") };
      CHECK(a.hash() != b.hash());
    }

    SECTION("Input") {
      b.inputs.clear();
      CHECK(a.hash() != b.hash());
    }

    SECTION("ImplicitInputs") {
      b.implicit_inputs.clear();
      CHECK(a.hash() != b.hash());
    }

    SECTION("Dependencies") {
      b.dependencies.clear();
      CHECK(a.hash() != b.hash());
    }

    SECTION("Outputs") {
      b.outputs.clear();
      CHECK(a.hash() != b.hash());
    }

    SECTION("PoolName") {
      // The pool is not significant for the build results
      b.pool_name.clear();
      CHECK(a.hash() == b.hash());
    }

    SECTION("Command") {
      b.command.clear();
      CHECK(a.hash() != b.hash());
    }

    SECTION("PoolName") {
      // The description is not significant for the build results
      b.description.clear();
      CHECK(a.hash() == b.hash());
    }

    SECTION("Depfile") {
      // The depfile path is not significant for the build results
      b.depfile = paths.get("other");
      CHECK(a.hash() == b.hash());
    }

    SECTION("Generator") {
      b.generator = !b.generator;
      CHECK(a.hash() != b.hash());
    }

    SECTION("GeneratorCommandline") {
      // Generator rules don't include the command line when calculating
      // dirtiness
      a.generator = b.generator = true;
      b.command = a.command + "somethingelse";
      CHECK(a.hash() == b.hash());
    }

    SECTION("Rspfile") {
      b.rspfile = "other";
      CHECK(a.hash() != b.hash());
    }

    SECTION("RspfileContent") {
      b.rspfile_content = "other";
      CHECK(a.hash() != b.hash());
    }
  }
}

}  // namespace shk
