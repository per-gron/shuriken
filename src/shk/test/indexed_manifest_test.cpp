#include <catch.hpp>

#include "indexed_manifest.h"

#include "in_memory_file_system.h"

namespace shk {
namespace detail {

TEST_CASE("IndexedManifest") {
  InMemoryFileSystem fs;
  Paths paths(fs);
  Manifest manifest;

  const Step empty{};

  Step single_output;
  single_output.outputs = { paths.get("a") };

  Step single_output_b;
  single_output_b.outputs = { paths.get("b") };

  Step multiple_outputs;
  multiple_outputs.outputs = { paths.get("c"), paths.get("d") };

  Step single_input;
  single_input.inputs = { paths.get("a") };

  Step single_implicit_input;
  single_implicit_input.implicit_inputs = { paths.get("a") };

  Step single_dependency;
  single_dependency.dependencies = { paths.get("a") };

  SECTION("computeOutputFileMap") {
    SECTION("basics") {
      CHECK(computeOutputFileMap({}).empty());
      CHECK(computeOutputFileMap({ empty }).empty());
      CHECK(computeOutputFileMap({ single_input }).empty());
      CHECK(computeOutputFileMap({ single_implicit_input }).empty());
      CHECK(computeOutputFileMap({ single_dependency }).empty());
    }

    SECTION("single output") {
      const auto map = computeOutputFileMap({ single_output });
      CHECK(map.size() == 1);
      const auto it = map.find(paths.get("a"));
      REQUIRE(it != map.end());
      CHECK(it->second == 0);
    }

    SECTION("multiple outputs") {
      auto map = computeOutputFileMap({
          single_output, single_output_b, multiple_outputs });
      CHECK(map.size() == 4);
      CHECK(map[paths.get("a")] == 0);
      CHECK(map[paths.get("b")] == 1);
      CHECK(map[paths.get("c")] == 2);
      CHECK(map[paths.get("d")] == 2);
    }

    SECTION("duplicate outputs") {
      CHECK_THROWS_AS(
          computeOutputFileMap({ single_output, single_output }), BuildError);
    }
  }

  SECTION("computeStepHashes") {
    CHECK(computeStepHashes({}).empty());
    CHECK(
        computeStepHashes({ single_output }) ==
        StepHashes{ single_output.hash() });
    CHECK(
        computeStepHashes({ single_output, single_input }) ==
        (StepHashes{ single_output.hash(), single_input.hash() }));
  }

  SECTION("DefaultConstructor") {
    IndexedManifest indexed_manifest;
  }

  SECTION("Constructor") {
    Manifest manifest;
    manifest.steps = { single_output };

    IndexedManifest indexed_manifest(std::move(manifest));

    CHECK(indexed_manifest.output_file_map.size() == 1);
    const auto it = indexed_manifest.output_file_map.find(paths.get("a"));
    REQUIRE(it != indexed_manifest.output_file_map.end());
    CHECK(it->second == 0);

    CHECK(indexed_manifest.step_hashes == StepHashes{ single_output.hash() });
  }
}

}  // namespace detail
}  // namespace shk
