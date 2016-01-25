#include <catch.hpp>

#include "build.h"

#include "generators.h"
#include "in_memory_file_system.h"

namespace shk {
namespace detail {
namespace {

std::vector<StepIndex> rootSteps(
    const std::vector<Step> &steps) {
  return ::shk::detail::rootSteps(steps, computeOutputFileMap(steps));
}

}  // anonymous namespace

TEST_CASE("Build") {
  InMemoryFileSystem fs;
  Paths paths(fs);

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

  SECTION("rootSteps") {
    CHECK(rootSteps({}).empty());
    CHECK(rootSteps({ single_output }) == std::vector<StepIndex>{ 0 });
    CHECK(
        rootSteps({ single_output, single_output_b }) ==
        (std::vector<StepIndex>{ 0, 1 }));
    CHECK(
        rootSteps({ single_output, single_input }) ==
        std::vector<StepIndex>{ 1 });
    CHECK(
        rootSteps({ single_output, single_implicit_input }) ==
        std::vector<StepIndex>{ 1 });
    CHECK(
        rootSteps({ single_output, single_dependency }) ==
        std::vector<StepIndex>{ 1 });
    CHECK(
        rootSteps({ single_dependency, single_output }) ==
        std::vector<StepIndex>{ 0 });
    CHECK(
        rootSteps({ single_dependency, single_output, multiple_outputs }) ==
        (std::vector<StepIndex>{ 0, 2 }));
  }

  SECTION("computeStepsToBuild") {
  }

  SECTION("computeReadySteps") {
  }

  SECTION("cycleErrorMessage") {
  }

  SECTION("computeBuild") {
  }

  SECTION("computeInvocationEntry") {
  }

  SECTION("computeStepHashes") {
  }

  SECTION("isClean") {
  }

  SECTION("computeCleanSteps") {
  }

  SECTION("discardCleanSteps") {
  }

  SECTION("outputsWereChanged") {
  }

  SECTION("deleteOldOutputs") {
  }

  SECTION("deleteStaleOutputs") {
  }

  SECTION("build") {
  }
}

}  // namespace detail
}  // namespace shk
