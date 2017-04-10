#include <catch.hpp>

#include "manifest/indexed_manifest.h"

#include "../in_memory_file_system.h"

namespace shk {
namespace detail {

TEST_CASE("IndexedManifest") {
  InMemoryFileSystem fs;
  Paths paths(fs);
  RawManifest manifest;

  const RawStep empty{};

  RawStep single_output;
  single_output.outputs = { paths.get("a") };

  RawStep single_output_b;
  single_output_b.outputs = { paths.get("b") };

  RawStep multiple_outputs;
  multiple_outputs.outputs = { paths.get("c"), paths.get("d") };

  RawStep single_input;
  single_input.inputs = { paths.get("a") };

  RawStep single_implicit_input;
  single_implicit_input.implicit_inputs = { paths.get("a") };

  RawStep single_dependency;
  single_dependency.dependencies = { paths.get("a") };

  SECTION("computeOutputFileMap") {
    SECTION("basics") {
      CHECK(computeOutputFileMap(std::vector<RawStep>()).empty());
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

  SECTION("DefaultConstructor") {
    IndexedManifest indexed_manifest;
  }

  SECTION("Constructor") {
    SECTION("basics") {
      RawManifest manifest;
      manifest.steps = { single_output };

      IndexedManifest indexed_manifest(std::move(manifest));

      CHECK(indexed_manifest.output_file_map.size() == 1);
      const auto it = indexed_manifest.output_file_map.find(paths.get("a"));
      REQUIRE(it != indexed_manifest.output_file_map.end());
      CHECK(it->second == 0);

      REQUIRE(indexed_manifest.steps.size() == 1);
      CHECK(indexed_manifest.steps[0].hash == single_output.hash());
    }

    SECTION("inputs") {
      RawManifest manifest;
      manifest.steps = { single_input };

      IndexedManifest indexed_manifest(std::move(manifest));
      REQUIRE(indexed_manifest.steps.size() == 1);
      CHECK(
          indexed_manifest.steps[0].dependencies ==
          std::vector<Path>{ paths.get("a") });
    }

    SECTION("implicit inputs") {
      RawManifest manifest;
      manifest.steps = { single_implicit_input };

      IndexedManifest indexed_manifest(std::move(manifest));
      REQUIRE(indexed_manifest.steps.size() == 1);
      CHECK(
          indexed_manifest.steps[0].dependencies ==
          std::vector<Path>{ paths.get("a") });
    }

    SECTION("dependencies") {
      RawManifest manifest;
      manifest.steps = { single_dependency };

      IndexedManifest indexed_manifest(std::move(manifest));
      REQUIRE(indexed_manifest.steps.size() == 1);
      CHECK(
          indexed_manifest.steps[0].dependencies ==
          std::vector<Path>{ paths.get("a") });
    }
  }
}

}  // namespace detail
}  // namespace shk
