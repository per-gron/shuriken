#include <catch.hpp>

#include "build.h"

#include "generators.h"
#include "in_memory_file_system.h"
#include "in_memory_invocation_log.h"

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
  time_t time = 555;
  const auto clock = [&time]() { return time; };
  InMemoryFileSystem fs(clock);
  Paths paths(fs);
  InMemoryInvocationLog log;
  Invocations invocations;
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

  SECTION("interpretPath") {
    Step other_input;
    other_input.inputs = { paths.get("other") };
    other_input.outputs = { paths.get("foo") };

    Step multiple_outputs;
    multiple_outputs.inputs = { paths.get("hehe") };
    multiple_outputs.outputs = { paths.get("hej"), paths.get("there") };

    Step implicit_input;
    implicit_input.implicit_inputs = { paths.get("implicit_input") };
    implicit_input.outputs = { paths.get("implicit_output") };

    Step dependency;
    dependency.dependencies = { paths.get("dependency_input") };
    dependency.outputs = { paths.get("dependency_output") };

    manifest.steps = {
        single_output,
        single_output_b,
        single_input,
        other_input,
        multiple_outputs,
        implicit_input,
        dependency };

    SECTION("normal (non-^)") {
      CHECK(interpretPath(paths, manifest, "a") == paths.get("a"));
      CHECK_THROWS_AS(interpretPath(paths, manifest, "x"), BuildError);
      CHECK_THROWS_AS(interpretPath(paths, manifest, "other"), BuildError);
    }

    SECTION("^") {
      CHECK_THROWS_AS(
          interpretPath(paths, manifest, "fancy_schmanzy^"), BuildError);
      CHECK(interpretPath(paths, manifest, "other^") == paths.get("foo"));
      CHECK_THROWS_AS(
          interpretPath(paths, manifest, "a^"), BuildError);  // No out edge
      CHECK(interpretPath(paths, manifest, "hehe^") == paths.get("hej"));
      CHECK(
          interpretPath(paths, manifest, "implicit_input^") ==
          paths.get("implicit_output"));
      CHECK(
          interpretPath(paths, manifest, "dependency_input^") ==
          paths.get("dependency_output"));
    }

    SECTION("clean") {
      try {
        interpretPath(paths, manifest, "clean");
        CHECK(!"Should throw");
      } catch (const BuildError &error) {
        CHECK(error.what() == std::string(
            "unknown target 'clean', did you mean 'shk -t clean'?"));
      }
    }

    SECTION("help") {
      try {
        interpretPath(paths, manifest, "help");
        CHECK(!"Should throw");
      } catch (const BuildError &error) {
        CHECK(error.what() == std::string(
            "unknown target 'help', did you mean 'shk -h'?"));
      }
    }
  }

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
      manifest.defaults = { paths.get("missing") };
      CHECK_THROWS_AS(computeStepsToBuild(manifest), BuildError);
    }

    SECTION("defaults") {
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
      CHECK(entry.output_files[0].second == takeFingerprint(fs, 555, "c"));

      REQUIRE(entry.input_files.size() == 2);
      CHECK(entry.input_files[0].first == "a");
      CHECK(entry.input_files[0].second == takeFingerprint(fs, 555, "a"));
      CHECK(entry.input_files[1].first == "b");
      CHECK(entry.input_files[1].second == takeFingerprint(fs, 555, "b"));
    }

    SECTION("missing files") {
      CommandRunner::Result result;
      result.output_files = { "a" };
      result.input_files = { "b" };

      const auto entry = computeInvocationEntry(clock, fs, result);

      REQUIRE(entry.output_files.size() == 1);
      CHECK(entry.output_files[0].first == "a");
      CHECK(entry.output_files[0].second == takeFingerprint(fs, 555, "a"));

      REQUIRE(entry.input_files.size() == 1);
      CHECK(entry.input_files[0].first == "b");
      CHECK(entry.input_files[0].second == takeFingerprint(fs, 555, "b"));
    }
  }

  SECTION("isClean") {
    Hash hash_a;
    std::fill(hash_a.data.begin(), hash_a.data.end(), 123);
    Hash hash_b;
    std::fill(hash_a.data.begin(), hash_a.data.end(), 321);
    Hash hash_c;
    std::fill(hash_a.data.begin(), hash_a.data.end(), 0);

    fs.writeFile("one", "one_content");
    const auto one_fp = takeFingerprint(fs, clock() + 1, "one");
    const auto one_fp_racy = takeFingerprint(fs, clock(), "one");
    fs.writeFile("two", "two_content");
    const auto two_fp = takeFingerprint(fs, clock() + 1, "two");


    SECTION("no matching Invocation entry") {
      CHECK(!isClean(
          clock,
          fs,
          log,
          invocations,
          hash_a));
      CHECK(log.createdDirectories().empty());
      CHECK(log.entries().empty());
    }

    SECTION("no input or output files") {
      invocations.entries[hash_a] = Invocations::Entry();
      CHECK(isClean(
          clock,
          fs,
          log,
          invocations,
          hash_a));
      CHECK(log.createdDirectories().empty());
      CHECK(log.entries().empty());
    }

    SECTION("clean input") {
      Invocations::Entry entry;
      entry.input_files.emplace_back(paths.get("one"), one_fp);
      invocations.entries[hash_a] = entry;
      CHECK(isClean(
          clock,
          fs,
          log,
          invocations,
          hash_a));
      CHECK(log.createdDirectories().empty());
      CHECK(log.entries().empty());
    }

    SECTION("dirty input") {
      Invocations::Entry entry;
      entry.input_files.emplace_back(paths.get("one"), one_fp);
      invocations.entries[hash_a] = entry;
      fs.writeFile("one", "dirty");  // Make dirty
      CHECK(!isClean(
          clock,
          fs,
          log,
          invocations,
          hash_a));
      CHECK(log.createdDirectories().empty());
      CHECK(log.entries().empty());
    }

    SECTION("clean output") {
      Invocations::Entry entry;
      entry.output_files.emplace_back(paths.get("one"), one_fp);
      invocations.entries[hash_a] = entry;
      CHECK(isClean(
          clock,
          fs,
          log,
          invocations,
          hash_a));
      CHECK(log.createdDirectories().empty());
      CHECK(log.entries().empty());
    }

    SECTION("dirty output") {
      Invocations::Entry entry;
      entry.output_files.emplace_back(paths.get("one"), one_fp);
      invocations.entries[hash_a] = entry;
      fs.writeFile("one", "dirty");  // Make dirty
      CHECK(!isClean(
          clock,
          fs,
          log,
          invocations,
          hash_a));
      CHECK(log.createdDirectories().empty());
      CHECK(log.entries().empty());
    }

    SECTION("dirty input and output") {
      Invocations::Entry entry;
      entry.output_files.emplace_back(paths.get("one"), one_fp);
      entry.input_files.emplace_back(paths.get("two"), two_fp);
      invocations.entries[hash_a] = entry;
      fs.writeFile("one", "dirty");
      fs.writeFile("two", "dirty!");
      CHECK(!isClean(
          clock,
          fs,
          log,
          invocations,
          hash_a));
      CHECK(log.createdDirectories().empty());
      CHECK(log.entries().empty());
    }

    SECTION("racily clean input") {
      Invocations::Entry entry;
      entry.input_files.emplace_back(paths.get("one"), one_fp_racy);
      invocations.entries[hash_a] = entry;
      CHECK(isClean(
          clock,
          fs,
          log,
          invocations,
          hash_a));
      CHECK(log.createdDirectories().empty());
      REQUIRE(log.entries().count(hash_a) == 1);
      const auto &computed_entry = log.entries().find(hash_a)->second;
      REQUIRE(computed_entry.input_files.size() == 1);
      REQUIRE(computed_entry.input_files[0].first == "one");
      CHECK(computed_entry.output_files.empty());
    }

    SECTION("racily clean output") {
      Invocations::Entry entry;
      entry.output_files.emplace_back(paths.get("one"), one_fp_racy);
      invocations.entries[hash_a] = entry;
      CHECK(isClean(
          clock,
          fs,
          log,
          invocations,
          hash_a));
      CHECK(log.createdDirectories().empty());
      REQUIRE(log.entries().count(hash_a) == 1);
      const auto &computed_entry = log.entries().find(hash_a)->second;
      CHECK(computed_entry.input_files.empty());
      REQUIRE(computed_entry.output_files.size() == 1);
      REQUIRE(computed_entry.output_files[0].first == "one");
    }
  }

  SECTION("computeCleanSteps") {
    SECTION("empty input") {
      CHECK(computeCleanSteps(
          clock,
          fs,
          log,
          invocations,
          StepHashes(),
          Build()).empty());
    }

    SECTION("should compute clean steps") {
      manifest.steps = { single_output_b, multiple_outputs };
      // Add empty entry to mark clean
      invocations.entries[single_output_b.hash()];

      const auto build = computeBuild(manifest, invocations);
      const auto clean_steps = computeCleanSteps(
          clock,
          fs,
          log,
          invocations,
          computeStepHashes(manifest.steps),
          build);

      REQUIRE(clean_steps.size() == 2);
      CHECK(clean_steps[0]);
      CHECK(!clean_steps[1]);
    }

    SECTION("don't compute for steps that should not be built") {
      manifest.steps = { single_output_b, multiple_outputs };
      manifest.defaults = { paths.get("b") };
      // Add empty entry to mark clean
      invocations.entries[single_output_b.hash()];

      const auto build = computeBuild(manifest, invocations);
      const auto clean_steps = computeCleanSteps(
          clock,
          fs,
          log,
          invocations,
          computeStepHashes(manifest.steps),
          build);

      REQUIRE(clean_steps.size() == 2);
      CHECK(clean_steps[0]);
      CHECK(!clean_steps[1]);
    }
  }

  SECTION("discardCleanSteps") {
    const auto compute_clean_steps = [&](
        const Build &build,
        Invocations &invocations,
        const Manifest &manifest) {
      return computeCleanSteps(
          clock,
          fs,
          log,
          invocations,
          computeStepHashes(manifest.steps),
          build);
    };

    SECTION("empty input") {
      Build build;
      discardCleanSteps(CleanSteps(), build);
    }

    SECTION("all clean (independent)") {
      manifest.steps = { single_output_b, multiple_outputs };
      // Add empty entry to mark clean
      invocations.entries[single_output_b.hash()];
      invocations.entries[multiple_outputs.hash()];
      auto build = computeBuild(manifest, invocations);
      CHECK(build.ready_steps.size() == 2);
      discardCleanSteps(
          compute_clean_steps(build, invocations, manifest),
          build);
      CHECK(build.ready_steps.empty());
    }

    SECTION("all dirty") {
      manifest.steps = { single_output_b, multiple_outputs };
      auto build = computeBuild(manifest, invocations);
      CHECK(build.ready_steps.size() == 2);
      discardCleanSteps(
          compute_clean_steps(build, invocations, manifest),
          build);
      CHECK(build.ready_steps.size() == 2);
    }

    SECTION("some clean") {
      manifest.steps = { single_output_b, multiple_outputs };
      // Add empty entry to mark clean
      invocations.entries[single_output_b.hash()];
      auto build = computeBuild(manifest, invocations);
      CHECK(build.ready_steps.size() == 2);
      discardCleanSteps(
          compute_clean_steps(build, invocations, manifest),
          build);
      CHECK(build.ready_steps.size() == 1);
    }

    Step root;
    root.inputs = { paths.get("a") };
    root.outputs = { paths.get("b") };

    SECTION("all clean") {
      manifest.steps = { single_output, root };
      // Add empty entry to mark clean
      invocations.entries[single_output.hash()];
      invocations.entries[root.hash()];
      auto build = computeBuild(manifest, invocations);
      CHECK(build.ready_steps.size() == 1);
      discardCleanSteps(
          compute_clean_steps(build, invocations, manifest),
          build);
      CHECK(build.ready_steps.empty());
    }

    SECTION("leave clean, root dirty") {
      manifest.steps = { single_output, root };
      // Add empty entry to mark clean
      invocations.entries[single_output.hash()];
      auto build = computeBuild(manifest, invocations);
      REQUIRE(build.ready_steps.size() == 1);
      CHECK(build.ready_steps[0] == 0);
      discardCleanSteps(
          compute_clean_steps(build, invocations, manifest),
          build);
      REQUIRE(build.ready_steps.size() == 1);
      CHECK(build.ready_steps[0] == 1);
    }

    SECTION("leave dirty, root clean") {
      manifest.steps = { single_output, root };
      // Add empty entry to mark clean
      invocations.entries[single_input.hash()];
      auto build = computeBuild(manifest, invocations);
      REQUIRE(build.ready_steps.size() == 1);
      CHECK(build.ready_steps[0] == 0);
      discardCleanSteps(
          compute_clean_steps(build, invocations, manifest),
          build);
      REQUIRE(build.ready_steps.size() == 1);
      CHECK(build.ready_steps[0] == 0);
    }
  }

  SECTION("outputsWereChanged") {
    Hash hash_a;
    std::fill(hash_a.data.begin(), hash_a.data.end(), 123);
    Hash hash_b;
    std::fill(hash_a.data.begin(), hash_a.data.end(), 321);
    Hash hash_c;
    std::fill(hash_a.data.begin(), hash_a.data.end(), 0);

    fs.writeFile("one", "one_content");
    const auto one_fp = takeFingerprint(fs, clock() + 1, "one");
    const auto one_fp_racy = takeFingerprint(fs, clock(), "one");
    fs.writeFile("two", "two_content");
    const auto two_fp = takeFingerprint(fs, clock() + 1, "two");


    SECTION("no matching Invocation entry") {
      CHECK(outputsWereChanged(fs, invocations, hash_a));
    }

    SECTION("no input or output files") {
      invocations.entries[hash_a] = Invocations::Entry();
      CHECK(!outputsWereChanged(fs, invocations, hash_a));
    }

    SECTION("clean input") {
      Invocations::Entry entry;
      entry.input_files.emplace_back(paths.get("one"), one_fp);
      invocations.entries[hash_a] = entry;
      CHECK(!outputsWereChanged(fs, invocations, hash_a));
    }

    SECTION("dirty input") {
      Invocations::Entry entry;
      entry.input_files.emplace_back(paths.get("one"), one_fp);
      invocations.entries[hash_a] = entry;
      fs.writeFile("one", "dirty");  // Make dirty
      CHECK(!outputsWereChanged(fs, invocations, hash_a));
    }

    SECTION("clean output") {
      Invocations::Entry entry;
      entry.output_files.emplace_back(paths.get("one"), one_fp);
      invocations.entries[hash_a] = entry;
      CHECK(!outputsWereChanged(fs, invocations, hash_a));
    }

    SECTION("dirty output") {
      Invocations::Entry entry;
      entry.output_files.emplace_back(paths.get("one"), one_fp);
      invocations.entries[hash_a] = entry;
      fs.writeFile("one", "dirty");  // Make dirty
      CHECK(outputsWereChanged(fs, invocations, hash_a));
    }

    SECTION("dirty input and output") {
      Invocations::Entry entry;
      entry.output_files.emplace_back(paths.get("one"), one_fp);
      entry.input_files.emplace_back(paths.get("two"), two_fp);
      invocations.entries[hash_a] = entry;
      fs.writeFile("one", "dirty");
      fs.writeFile("two", "dirty!");
      CHECK(outputsWereChanged(fs, invocations, hash_a));
    }
  }

  SECTION("deleteOldOutputs") {
    // TODO(peck): Test this
  }

  SECTION("deleteStaleOutputs") {
    // TODO(peck): Test this
  }

  SECTION("build") {
    // TODO(peck): Test this
  }
}

}  // namespace detail
}  // namespace shk
