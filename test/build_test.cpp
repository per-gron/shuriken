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

std::vector<StepIndex> computeStepsToBuild(
    const Manifest &manifest) throw(BuildError) {
  return ::shk::detail::computeStepsToBuild(
      manifest, computeOutputFileMap(manifest.steps));
}

std::vector<StepIndex> vec(const std::vector<StepIndex> &vec) {
  return vec;
}

Build computeBuild(
    const Manifest &manifest,
    const Invocations &invocations = Invocations(),
    size_t allowed_failures = 1) throw(BuildError) {
  const auto output_file_map = computeOutputFileMap(manifest.steps);
  return ::shk::detail::computeBuild(
      computeStepHashes(manifest.steps),
      invocations,
      output_file_map,
      manifest,
      allowed_failures,
      ::shk::detail::computeStepsToBuild(manifest, output_file_map));
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
        vec({ 0, 1 }));
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
    SECTION("trivial") {
      CHECK(computeStepsToBuild(Manifest()).empty());
    }

    SECTION("invalid defaults") {
      Manifest manifest;
      manifest.defaults = { paths.get("missing") };
      CHECK_THROWS_AS(computeStepsToBuild(manifest), BuildError);
    }

    SECTION("defaults") {
      Manifest manifest;
      manifest.steps = { single_output_b, multiple_outputs };

      manifest.defaults = { paths.get("b") };
      CHECK(computeStepsToBuild(manifest) == vec({0}));

      manifest.defaults = { paths.get("c") };
      CHECK(computeStepsToBuild(manifest) == vec({1}));

      manifest.defaults = { paths.get("d") };
      CHECK(computeStepsToBuild(manifest) == vec({1}));

      manifest.defaults = { paths.get("d"), paths.get("c") };
      // Duplicates are ok. We could deduplicate but that would just be an
      // unnecessary expense.
      CHECK(computeStepsToBuild(manifest) == vec({1, 1}));

      manifest.defaults = { paths.get("b"), paths.get("c") };
      CHECK(computeStepsToBuild(manifest) == vec({0, 1}));
    }

    SECTION("use root steps when defaults are missing") {
      Manifest manifest;
      manifest.steps = { single_output, single_input };
      CHECK(computeStepsToBuild(manifest) == vec({1}));
    }
  }

  SECTION("cycleErrorMessage") {
    CHECK(
        cycleErrorMessage({ paths.get("a") }) == "a -> a");
    CHECK(
        cycleErrorMessage({ paths.get("a"), paths.get("b") }) == "a -> b -> a");
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

  SECTION("computeBuild") {
    SECTION("empty") {
      const auto build = computeBuild(Manifest());
      CHECK(build.step_nodes.empty());
      CHECK(build.ready_steps.empty());
      CHECK(build.interrupted == false);
      CHECK(build.remaining_failures == 1);
    }

    SECTION("remaining_failures") {
      const auto build = computeBuild(Manifest(), Invocations(), 543);
      CHECK(build.remaining_failures == 543);
    }

    SECTION("ready_steps") {
      Manifest manifest;

      SECTION("basic") {
        manifest.steps = { single_output };
        CHECK(computeBuild(manifest).ready_steps == vec({ 0 }));
      }

      SECTION("two steps") {
        manifest.steps = { single_output, single_output_b };
        CHECK(computeBuild(manifest).ready_steps == vec({ 0, 1 }));
      }

      SECTION("single dep") {
        manifest.steps = { single_output, single_input };
        CHECK(computeBuild(manifest).ready_steps == vec({ 0 }));

        manifest.steps = { single_input, single_output };
        CHECK(computeBuild(manifest).ready_steps == vec({ 1 }));
      }

      SECTION("dep chain") {
        Step one;
        one.outputs = { paths.get("a") };
        Step two;
        two.inputs = { paths.get("a") };
        two.outputs = { paths.get("b") };
        Step three;
        three.inputs = { paths.get("b") };

        manifest.steps = { three, one, two };
        CHECK(computeBuild(manifest).ready_steps == vec({ 1 }));

        manifest.steps = { one, two, three };
        CHECK(computeBuild(manifest).ready_steps == vec({ 0 }));
      }

      SECTION("diamond dep") {
        Step one;
        one.outputs = { paths.get("a") };
        Step two_1;
        two_1.inputs = { paths.get("a") };
        two_1.outputs = { paths.get("b") };
        Step two_2;
        two_2.inputs = { paths.get("a") };
        two_2.outputs = { paths.get("c") };
        Step three;
        three.inputs = { paths.get("b"), paths.get("c") };

        manifest.steps = { three, one, two_1, two_2 };
        CHECK(computeBuild(manifest).ready_steps == vec({ 1 }));

        manifest.steps = { three, two_2, two_1, one };
        CHECK(computeBuild(manifest).ready_steps == vec({ 3 }));
      }
    }

    SECTION("step_nodes.should_build") {
      Manifest manifest;

      Step one;
      one.outputs = { paths.get("a") };
      Step two;
      two.inputs = { paths.get("a") };
      two.outputs = { paths.get("b") };
      Step three;
      three.inputs = { paths.get("b") };

      SECTION("everything") {
        manifest.steps = { one, two, three };
        const auto build = computeBuild(manifest);
        REQUIRE(build.step_nodes.size() == 3);
        CHECK(build.step_nodes[0].should_build);
        CHECK(build.step_nodes[1].should_build);
        CHECK(build.step_nodes[2].should_build);
      }

      SECTION("just some") {
        manifest.steps = { one, two, three };
        manifest.defaults = { paths.get("b") };
        const auto build = computeBuild(manifest);
        REQUIRE(build.step_nodes.size() == 3);
        CHECK(build.step_nodes[0].should_build);
        CHECK(build.step_nodes[1].should_build);
        CHECK(!build.step_nodes[2].should_build);
      }
    }

    SECTION("dependencies") {
      Manifest manifest;

      SECTION("independent") {
        manifest.steps = { single_output, single_output_b };
        const auto build = computeBuild(manifest);
        REQUIRE(build.step_nodes.size() == 2);

        CHECK(build.step_nodes[0].dependencies == 0);
        CHECK(build.step_nodes[0].dependents == vec({}));

        CHECK(build.step_nodes[1].dependencies == 0);
        CHECK(build.step_nodes[1].dependents == vec({}));
      }

      SECTION("diamond") {
        Step one;
        one.outputs = { paths.get("a") };
        Step two_1;
        two_1.inputs = { paths.get("a") };
        two_1.outputs = { paths.get("b") };
        Step two_2;
        two_2.inputs = { paths.get("a") };
        two_2.outputs = { paths.get("c") };
        Step three;
        three.inputs = { paths.get("b"), paths.get("c") };

        manifest.steps = { three, two_2, two_1, one };
        const auto build = computeBuild(manifest);
        REQUIRE(build.step_nodes.size() == 4);

        // three
        CHECK(build.step_nodes[0].dependencies == 2);
        CHECK(build.step_nodes[0].dependents == vec({}));

        // two_2
        CHECK(build.step_nodes[1].dependencies == 1);
        CHECK(build.step_nodes[1].dependents == vec({0}));

        // two_1
        CHECK(build.step_nodes[2].dependencies == 1);
        CHECK(build.step_nodes[2].dependents == vec({0}));

        // one
        CHECK(build.step_nodes[3].dependencies == 0);
        CHECK(build.step_nodes[3].dependents == vec({2, 1}));
      }
    }

    SECTION("Deps from invocations") {
      Step three;
      three.inputs = { paths.get("a"), paths.get("b") };

      Invocations::Entry entry;
      // Didn't read all declared inputs
      entry.input_files = { { paths.get("a"), Fingerprint() } };
      Invocations invocations;
      invocations.entries[three.hash()] = entry;

      Manifest manifest;
      manifest.steps = { single_output, single_output_b, three };
      const auto build = computeBuild(manifest, invocations);
      REQUIRE(build.step_nodes.size() == 3);

      CHECK(build.step_nodes[0].dependencies == 0);
      CHECK(build.step_nodes[0].dependents == vec({2}));

      CHECK(build.step_nodes[1].dependencies == 0);
      CHECK(build.step_nodes[1].dependents == vec({}));

      CHECK(build.step_nodes[2].dependencies == 1);
      CHECK(build.step_nodes[2].dependents == vec({}));
    }

    SECTION("Dependency cycle") {
      Step one;
      one.outputs = { paths.get("a") };
      one.inputs = { paths.get("b") };
      Step two;
      two.inputs = { paths.get("a") };
      two.outputs = { paths.get("b") };

      Manifest manifest;
      // Need to specify a default, otherwise none of the steps are roots, and
      // nothing is "built".
      manifest.defaults = { paths.get("a") };
      manifest.steps = { one, two };
      CHECK_THROWS_AS(computeBuild(manifest), BuildError);
    }
  }

  SECTION("computeInvocationEntry") {
    InMemoryFileSystem fs;
    const Clock clock = []{ return 432; };

    SECTION("empty") {
      const auto entry = computeInvocationEntry(
          clock, fs, CommandRunner::Result());
      CHECK(entry.output_files.empty());
      CHECK(entry.input_files.empty());
    }

    SECTION("files") {
      fs.writeFile("a", "!");
      fs.writeFile("b", "?");
      fs.writeFile("c", ":");

      CommandRunner::Result result;
      result.output_files = { "c" };
      result.input_files = { "a", "b" };

      const auto entry = computeInvocationEntry(clock, fs, result);

      REQUIRE(entry.output_files.size() == 1);
      CHECK(entry.output_files[0].first == "c");
      CHECK(entry.output_files[0].second == takeFingerprint(fs, 432, "c"));

      REQUIRE(entry.input_files.size() == 2);
      CHECK(entry.input_files[0].first == "a");
      CHECK(entry.input_files[0].second == takeFingerprint(fs, 432, "a"));
      CHECK(entry.input_files[1].first == "b");
      CHECK(entry.input_files[1].second == takeFingerprint(fs, 432, "b"));
    }

    SECTION("missing files") {
      CommandRunner::Result result;
      result.output_files = { "a" };
      result.input_files = { "b" };

      const auto entry = computeInvocationEntry(clock, fs, result);

      REQUIRE(entry.output_files.size() == 1);
      CHECK(entry.output_files[0].first == "a");
      CHECK(entry.output_files[0].second == takeFingerprint(fs, 432, "a"));

      REQUIRE(entry.input_files.size() == 1);
      CHECK(entry.input_files[0].first == "b");
      CHECK(entry.input_files[0].second == takeFingerprint(fs, 432, "b"));
    }
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
