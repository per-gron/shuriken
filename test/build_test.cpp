#include <catch.hpp>

#include "build.h"

#include "dummy_build_status.h"
#include "dummy_command_runner.h"
#include "generators.h"
#include "in_memory_file_system.h"
#include "in_memory_invocation_log.h"

namespace shk {
namespace detail {
namespace {

class FailingCommandRunner : public CommandRunner {
 public:
  void invoke(
      const std::string &command,
      UseConsole use_console,
      const Callback &callback) override {
    if (!command.empty()) {
      CHECK(!"Should not be invoked");
    }
  }

  size_t size() const override { return 0; }
  bool canRunMore() const override { return true; }
  bool runCommands() override { return false; }
};

/**
 * CommandRunner that asserts that no more than the given number of commands
 * is run at any given time. This is useful when verifying that the build does
 * not have too much parallelism (as in so much that the build is wrong).
 */
class MaxCapacityCommandRunner : public CommandRunner {
 public:
  MaxCapacityCommandRunner(size_t max_capacity, CommandRunner &inner)
      : _max_capacity(max_capacity), _inner(inner) {}

  void invoke(
      const std::string &command,
      UseConsole use_console,
      const Callback &callback) override {
    CHECK(_inner.size() < _max_capacity);
    _inner.invoke(command, use_console, callback);
  }

  size_t size() const override {
    return _inner.size();
  }

  bool canRunMore() const override {
    return _inner.canRunMore();
  }

  bool runCommands() override {
    return _inner.runCommands();
  }

 private:
  const size_t _max_capacity;
  CommandRunner &_inner;
};

std::vector<StepIndex> rootSteps(
    const std::vector<Step> &steps) {
  return ::shk::detail::rootSteps(steps, computeOutputFileMap(steps));
}

std::vector<StepIndex> computeStepsToBuild(
    const Manifest &manifest,
    const std::vector<Path> &specified_outputs = {}) throw(BuildError) {
  return ::shk::detail::computeStepsToBuild(
      manifest, computeOutputFileMap(manifest.steps), specified_outputs);
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
      ::shk::detail::computeStepsToBuild(manifest, output_file_map, {}));
}

}  // anonymous namespace

TEST_CASE("Build") {
  time_t time = 555;
  const auto clock = [&time]() { return time; };
  InMemoryFileSystem fs(clock);
  Paths paths(fs);
  InMemoryInvocationLog log(fs, clock);
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

  const auto parse = [&](const std::string &input) {
    fs.writeFile("build.ninja", input);
    return parseManifest(paths, fs, "build.ninja");
  };

  DummyCommandRunner dummy_runner(fs);

  const auto build_or_rebuild_manifest = [&](
      const std::string &manifest,
      size_t failures_allowed,
      CommandRunner &runner) {
    return build(
        clock,
        fs,
        runner,
        [](int total_steps) {
          return std::unique_ptr<BuildStatus>(
              new DummyBuildStatus());
        },
        log,
        failures_allowed,
        {},
        parse(manifest),
        log.invocations(paths));
  };

  const auto build_manifest = [&](
      const std::string &manifest,
      size_t failures_allowed = 1) {
    return build_or_rebuild_manifest(manifest, failures_allowed, dummy_runner);
  };

  const auto verify_noop_build = [&](
      const std::string &manifest,
      size_t failures_allowed = 1) {
    FailingCommandRunner failing_runner;
    CHECK(build_or_rebuild_manifest(
        manifest,
        failures_allowed,
        failing_runner) == BuildResult::NO_WORK_TO_DO);
  };

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

  SECTION("interpretPath") {
    SECTION("Empty") {
      CHECK(interpretPaths(paths, manifest, 0, nullptr).empty());
    }

    SECTION("Paths") {
      manifest.steps = {
          single_output,
          single_output_b };

      std::string a = "a";
      std::string b = "b";
      char *in[] = { &a[0], &b[0] };
      const std::vector<Path> out = { paths.get("a"), paths.get("b") };
      CHECK(interpretPaths(paths, manifest, 2, in) == out);
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

  SECTION("computeStepsToBuild helper") {

    manifest.steps = { single_output_b, multiple_outputs };

    // Kinda stupid test, yes I know. This is mostly just to get coverage, this
    // function is simple enough that I expect it to not have significant bugs.
    manifest.defaults = { paths.get("b") };
    CHECK(computeStepsToBuild(paths, manifest, 0, nullptr) == vec({0}));
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

    SECTION("specified_outputs") {
      manifest.steps = { single_output_b, multiple_outputs };

      CHECK(computeStepsToBuild(manifest, { paths.get("b") }) == vec({0}));
      CHECK(computeStepsToBuild(manifest, { paths.get("c") }) == vec({1}));
      CHECK(computeStepsToBuild(manifest, { paths.get("d") }) == vec({1}));

      // Duplicates are ok. We could deduplicate but that would just be an
      // unnecessary expense.
      CHECK(computeStepsToBuild(manifest, { paths.get("d"), paths.get("c") }) ==
          vec({1, 1}));

      CHECK(computeStepsToBuild(manifest, { paths.get("b"), paths.get("c") }) ==
          vec({0, 1}));
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
      CHECK(discardCleanSteps(CleanSteps(), build) == 0);
    }

    SECTION("all clean (independent)") {
      manifest.steps = { single_output_b, multiple_outputs };
      // Add empty entry to mark clean
      invocations.entries[single_output_b.hash()];
      invocations.entries[multiple_outputs.hash()];
      auto build = computeBuild(manifest, invocations);
      CHECK(build.ready_steps.size() == 2);
      CHECK(discardCleanSteps(
          compute_clean_steps(build, invocations, manifest),
          build) == 2);
      CHECK(build.ready_steps.empty());
    }

    SECTION("all dirty") {
      manifest.steps = { single_output_b, multiple_outputs };
      auto build = computeBuild(manifest, invocations);
      CHECK(build.ready_steps.size() == 2);
      CHECK(discardCleanSteps(
          compute_clean_steps(build, invocations, manifest),
          build) == 0);
      CHECK(build.ready_steps.size() == 2);
    }

    SECTION("some clean") {
      manifest.steps = { single_output_b, multiple_outputs };
      // Add empty entry to mark clean
      invocations.entries[single_output_b.hash()];
      auto build = computeBuild(manifest, invocations);
      CHECK(build.ready_steps.size() == 2);
      CHECK(discardCleanSteps(
          compute_clean_steps(build, invocations, manifest),
          build) == 1);
      CHECK(build.ready_steps.size() == 1);
    }

    Step root;
    root.inputs = { paths.get("a") };
    root.outputs = { paths.get("b") };

    SECTION("all clean") {
      manifest.steps = { single_output, root };
      // Add empty entry to mark clean
      invocations.entries[single_output.hash()];
      invocations.entries[root.hash()].input_files.emplace_back(
          single_output.outputs[0],
          Fingerprint());
      auto build = computeBuild(manifest, invocations);
      CHECK(build.ready_steps.size() == 1);
      CHECK(discardCleanSteps(
          compute_clean_steps(build, invocations, manifest),
          build) == 2);
      CHECK(build.ready_steps.empty());
    }

    SECTION("leaf clean, root dirty") {
      manifest.steps = { single_output, root };
      // Add empty entry to mark clean
      invocations.entries[single_output.hash()];
      auto build = computeBuild(manifest, invocations);
      REQUIRE(build.ready_steps.size() == 1);
      CHECK(build.ready_steps[0] == 0);
      CHECK(discardCleanSteps(
          compute_clean_steps(build, invocations, manifest),
          build) == 1);
      REQUIRE(build.ready_steps.size() == 1);
      CHECK(build.ready_steps[0] == 1);
    }

    SECTION("leaf dirty, root clean") {
      manifest.steps = { single_output, root };
      // Add empty entry to mark clean
      invocations.entries[single_input.hash()];
      auto build = computeBuild(manifest, invocations);
      REQUIRE(build.ready_steps.size() == 1);
      CHECK(build.ready_steps[0] == 0);
      CHECK(discardCleanSteps(
          compute_clean_steps(build, invocations, manifest),
          build) == 0);
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

  SECTION("countStepsToBuild") {
    // TODO(peck): Test this
  }

  SECTION("build") {
    SECTION("initial build") {
      SECTION("empty input") {
        const auto manifest = "";
        verify_noop_build(manifest);
      }

      SECTION("single successful step") {
        const auto cmd = dummy_runner.constructCommand({}, {"out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd);
      }

      SECTION("multiple outputs") {
        const auto cmd = dummy_runner.constructCommand({}, {"out1", "out2"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out1 out2: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd);
      }

      SECTION("single failing step") {
        const auto cmd = dummy_runner.constructCommand({"nonexisting"}, {});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::FAILURE);
      }

      SECTION("failing step and successful step") {
        const auto fail = dummy_runner.constructCommand({"nonexisting"}, {});
        const auto success = dummy_runner.constructCommand({}, {"out"});
        const auto manifest =
            "rule success\n"
            "  command = " + success + "\n"
            "rule fail\n"
            "  command = " + fail + "\n"
            "build out: success\n"
            "build out2: fail\n";
        CHECK(build_manifest(manifest) == BuildResult::FAILURE);
      }

      SECTION("independent failing steps") {
        const auto cmd = dummy_runner.constructCommand({"nonexisting"}, {});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out1: cmd\n"
            "build out2: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::FAILURE);
      }

      SECTION("two independent steps") {
        const auto one = dummy_runner.constructCommand({}, {"one"});
        const auto two = dummy_runner.constructCommand({}, {"two"});
        const auto manifest =
            "rule one\n"
            "  command = " + one + "\n"
            "rule two\n"
            "  command = " + two + "\n"
            "build one: one\n"
            "build two: two\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, one);
        dummy_runner.checkCommand(fs, two);
      }

      SECTION("two steps in a chain") {
        const auto one = dummy_runner.constructCommand({}, {"one"});
        const auto two = dummy_runner.constructCommand({"one"}, {"two"});
        const auto manifest =
            "rule one\n"
            "  command = " + one + "\n"
            "rule two\n"
            "  command = " + two + "\n"
            "build two: two one\n"
            "build one: one\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, one);
        dummy_runner.checkCommand(fs, two);
      }

      SECTION("diamond") {
        const auto one = dummy_runner.constructCommand({}, {"one"});
        const auto two = dummy_runner.constructCommand({"one"}, {"two"});
        const auto three = dummy_runner.constructCommand({"one"}, {"three"});
        const auto four = dummy_runner.constructCommand({"two", "three"}, {"four"});
        const auto manifest =
            "rule one\n"
            "  command = " + one + "\n"
            "rule two\n"
            "  command = " + two + "\n"
            "rule three\n"
            "  command = " + three + "\n"
            "rule four\n"
            "  command = " + four + "\n"
            "build three: three one\n"
            "build four: four two three\n"
            "build one: one\n"
            "build two: two one\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, one);
        dummy_runner.checkCommand(fs, two);
        dummy_runner.checkCommand(fs, three);
        dummy_runner.checkCommand(fs, four);
      }

      SECTION("first step failing in a chain") {
        const auto one = dummy_runner.constructCommand({"nonexisting"}, {"one"});
        const auto two = dummy_runner.constructCommand({}, {"two"});
        const auto manifest =
            "rule one\n"
            "  command = " + one + "\n"
            "rule two\n"
            "  command = " + two + "\n"
            "build two: two one\n"
            "build one: one\n";
        CHECK(build_manifest(manifest) == BuildResult::FAILURE);
        CHECK_THROWS(dummy_runner.checkCommand(fs, one));
        CHECK_THROWS(dummy_runner.checkCommand(fs, two));
      }

      SECTION("second step failing in a chain") {
        const auto one = dummy_runner.constructCommand({}, {"one"});
        const auto two = dummy_runner.constructCommand({"nonexisting"}, {"two"});
        const auto manifest =
            "rule one\n"
            "  command = " + one + "\n"
            "rule two\n"
            "  command = " + two + "\n"
            "build two: two one\n"
            "build one: one\n";
        CHECK(build_manifest(manifest) == BuildResult::FAILURE);
        dummy_runner.checkCommand(fs, one);
        CHECK_THROWS(dummy_runner.checkCommand(fs, two));
      }

#if 0  // TODO(peck): This test does not work because deletion is not yet implemented
      SECTION("don't treat depfile as output file") {
        const auto cmd = dummy_runner.constructCommand({}, { "out", "depfile" });
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "  depfile = depfile\n"
            "build out: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        REQUIRE(log.entries().size() == 1);
        const auto &entry = log.entries().begin()->second;
        CHECK(entry.input_files.empty());
        // depfile should not be in the list
        REQUIRE(entry.output_files.size() == 1);
        CHECK(entry.output_files[0].first == "out");
      }

      SECTION("delete depfile") {
        const auto cmd = dummy_runner.constructCommand({}, {"depfile"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "  depfile = depfile\n"
            "build out: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        CHECK(fs.stat("depfile").result == ENOENT);
      }
#endif

      SECTION("don't fail if depfile is not created") {
        const auto cmd = dummy_runner.constructCommand({}, {});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "  depfile = depfile\n"
            "build out: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        CHECK(fs.stat("depfile").result == ENOENT);
        dummy_runner.checkCommand(fs, cmd);
      }

#if 0  // TODO(peck): This test does not work because deletion is not yet implemented
      SECTION("create and delete rspfile") {
        const auto cmd = dummy_runner.constructCommand({"rsp"}, {});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "  rspfile = rsp\n"
            "  rspfile_content = abc\n"
            "build out: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        CHECK(fs.stat("rsp").result == ENOENT);
      }
#endif

      SECTION("don't delete rspfile on failure") {
        const auto cmd = dummy_runner.constructCommand({"nonexisting"}, {});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "  rspfile = rsp\n"
            "  rspfile_content = abc\n"
            "build out: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::FAILURE);
        CHECK(fs.readFile("rsp") == "abc");
      }

      SECTION("phony as root") {
        const auto one = dummy_runner.constructCommand({}, {"one"});
        const auto manifest =
            "rule one\n"
            "  command = " + one + "\n"
            "build two: phony one\n"
            "build one: one\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, one);
      }

      SECTION("phony as leaf") {
        const auto cmd = dummy_runner.constructCommand({}, {"out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build one: phony\n"
            "build two: cmd one\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd);
      }

      SECTION("don't fail on missing input") {
        // Ninja fails the build in this case. For Shuriken I can see no strong
        // reason to fail though, incremental builds work even when input files
        // are missing. If the input file is really needed then the build step
        // should fail anyway.
        //
        // If it turns out to be important to do the same thing as Ninja here,
        // it's probably no problem doing that either, it's just that I don't
        // feel like spending time on adding the additional logic and stat calls
        // for it right now.

        const auto cmd = dummy_runner.constructCommand({}, {"out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out: cmd missing\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd);
      }

      SECTION("don't fail on missing phony input") {
        const auto manifest =
            "build out: phony missing\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
      }

      SECTION("swallow failures") {
        const auto fail = dummy_runner.constructCommand({"nonexisting"}, {});
        const auto succeed = dummy_runner.constructCommand({}, {"out"});
        const auto manifest =
            "rule fail\n"
            "  command = " + fail + "\n"
            "rule succeed\n"
            "  command = " + succeed + "\n"
            "build out1: fail\n"
            "build out2: fail\n"
            "build out3: succeed\n";
        CHECK(build_manifest(manifest, 3) == BuildResult::FAILURE);
        dummy_runner.checkCommand(fs, succeed);
      }

      SECTION("swallow failures (2)") {
        const auto fail = dummy_runner.constructCommand({"nonexisting"}, {});
        const auto succeed = dummy_runner.constructCommand({}, {"out"});
        const auto manifest =
            "rule fail\n"
            "  command = " + fail + "\n"
            "rule succeed\n"
            "  command = " + succeed + "\n"
            "build out3: succeed\n"
            "build out1: fail\n"
            "build out2: fail\n";
        CHECK(build_manifest(manifest, 3) == BuildResult::FAILURE);
        dummy_runner.checkCommand(fs, succeed);
      }

      SECTION("don't swallow too many failures") {
        const auto fail = dummy_runner.constructCommand({"nonexisting"}, {});
        const auto succeed1 = dummy_runner.constructCommand({}, {"out1"});
        const auto succeed2 = dummy_runner.constructCommand({}, {"out2"});
        const auto manifest =
            "rule fail\n"
            "  command = " + fail + "\n"
            "rule succeed1\n"
            "  command = " + succeed1 + "\n"
            "rule succeed2\n"
            "  command = " + succeed2 + "\n"
            "build out1: fail\n"
            "build out2: fail\n"
            "build out3: succeed1\n"
            "build out4: succeed2 out3\n";
        CHECK(build_manifest(manifest, 2) == BuildResult::FAILURE);
        CHECK_THROWS(dummy_runner.checkCommand(fs, succeed2));
      }

      SECTION("swallow failures but don't run dependent steps") {
        const auto fail = dummy_runner.constructCommand({"nonexisting"}, {});
        const auto succeed = dummy_runner.constructCommand({}, {"out"});
        const auto manifest =
            "rule fail\n"
            "  command = " + fail + "\n"
            "rule succeed\n"
            "  command = " + succeed + "\n"
            "build out1: fail\n"
            "build out2: succeed out1\n";
        CHECK(build_manifest(manifest, 100) == BuildResult::FAILURE);
        CHECK_THROWS(dummy_runner.checkCommand(fs, succeed));
      }

      SECTION("implicit deps") {
        const auto one = dummy_runner.constructCommand({}, {"one"});
        const auto two = dummy_runner.constructCommand({"one"}, {"two"});
        const auto manifest =
            "rule one\n"
            "  command = " + one + "\n"
            "rule two\n"
            "  command = " + two + "\n"
            "build two: two | one\n"
            "build one: one\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, one);
        dummy_runner.checkCommand(fs, two);
      }

      SECTION("order-only deps") {
        const auto one = dummy_runner.constructCommand({}, {"one"});
        const auto two = dummy_runner.constructCommand({"one"}, {"two"});
        const auto manifest =
            "rule one\n"
            "  command = " + one + "\n"
            "rule two\n"
            "  command = " + two + "\n"
            "build two: two || one\n"
            "build one: one\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, one);
        dummy_runner.checkCommand(fs, two);
      }
    }

    SECTION("rebuild") {
      SECTION("rebuild is no-op") {
        const auto cmd = dummy_runner.constructCommand({}, {"out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd);

        verify_noop_build(manifest);
      }

      SECTION("rebuild with phony root is no-op") {
        const auto cmd = dummy_runner.constructCommand({}, {"out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out: cmd\n"
            "build root: phony out\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd);

        verify_noop_build(manifest);
      }

      SECTION("order-only deps rebuild") {
        // TODO(peck): Test this
      }

      SECTION("always rebuild console rule") {
        // TODO(peck): Test this
      }

      SECTION("always rebuild steps that depend on console rule") {
        // TODO(peck): Test this
      }

      SECTION("rebuild when step is different") {
        // TODO(peck): Test this
      }

      SECTION("rebuild when step failed") {
        // TODO(peck): Test this
      }

      SECTION("rebuild when step failed linting") {
        // TODO(peck): Test this
      }

      SECTION("delete stale outputs") {
        // TODO(peck): Test this
      }

      SECTION("delete outputs of removed step") {
        // TODO(peck): Test this
      }

      SECTION("rebuild when input file changed") {
        const auto cmd = dummy_runner.constructCommand({"in"}, {"out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out: cmd in\n";
        fs.writeFile("in", "before");
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        fs.writeFile("in", "after");
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd);
      }

      SECTION("rebuild when input file removed") {
        const auto cmd = dummy_runner.constructCommand({"in"}, {"out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out: cmd in\n";
        fs.writeFile("in", "before");
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        fs.unlink("in");
        CHECK(build_manifest(manifest) == BuildResult::FAILURE);
      }

      SECTION("rebuild when undeclared input file changed") {
        const auto cmd = dummy_runner.constructCommand({"in1","in2"}, {"out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out: cmd in1\n";
        fs.writeFile("in1", "input");
        fs.writeFile("in2", "before");
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        fs.writeFile("in2", "after");
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd);
      }

      SECTION("don't rebuild when declared but not used input changed") {
        const auto cmd = dummy_runner.constructCommand({"in"}, {"out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out: cmd unused_in\n";
        fs.writeFile("in", "input");
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        fs.writeFile("in", "after");
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd);
      }

      SECTION("rebuild when output changed") {
        const auto cmd = dummy_runner.constructCommand({}, {"out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        fs.writeFile("out", "dirty!");
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd);
      }

      SECTION("rebuild when output file removed") {
        const auto cmd = dummy_runner.constructCommand({}, {"out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        fs.unlink("out");
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd);
      }

      SECTION("rebuild when output file removed with phony root") {
        const auto cmd = dummy_runner.constructCommand({}, {"out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build out: cmd\n"
            "build root: phony out\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        fs.unlink("out");
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd);
      }

      SECTION("respect dependencies when rebuilding") {
        // Set-up
        const auto cmd1 = dummy_runner.constructCommand({}, {"out1"});
        const auto cmd2 = dummy_runner.constructCommand({"out1"}, {"out2"});
        const auto manifest =
            "rule cmd1\n"
            "  command = " + cmd1 + "\n"
            "rule cmd2\n"
            "  command = " + cmd2 + "\n"
            "build out1: cmd1\n"
            "build out2: cmd2 out1\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd1);
        dummy_runner.checkCommand(fs, cmd2);
        fs.writeFile("out1", "dirty");

        // Ok so here comes the test. The point of this test is that with this
        // set-up, both commands need to be re-run, but because of their
        // dependencies cmd1 must run strictly before cmd2.
        MaxCapacityCommandRunner cap_runner(1, dummy_runner);
        CHECK(build_or_rebuild_manifest(manifest, 1, cap_runner) ==
            BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd1);
        dummy_runner.checkCommand(fs, cmd2);
      }
    }

    SECTION("interrupted") {
      SECTION("delete depfile and rspfile after interruption") {
        // TODO(peck): Test this
      }

      SECTION("stop build after interruption") {
        // TODO(peck): Test this
      }

      SECTION("don't count interrupted command as built") {
        // TODO(peck): Test this
      }
    }

    SECTION("pools") {
      // TODO(peck): Test this
    }
  }
}

}  // namespace detail
}  // namespace shk
