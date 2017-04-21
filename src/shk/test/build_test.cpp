#include <catch.hpp>

#include <sys/stat.h>

#include "build.h"

#include "status/build_status.h"

#include "dummy_command_runner.h"
#include "in_memory_file_system.h"
#include "in_memory_invocation_log.h"
#include "manifest/step_builder.h"

namespace shk {
namespace detail {
namespace {

class OutputCapturerBuildStatus : public BuildStatus {
 public:
  OutputCapturerBuildStatus(std::vector<std::string> &latest_build_output)
      : _latest_build_output(latest_build_output) {
    latest_build_output.clear();
  }

  void stepStarted(const Step &step) override {}

  void stepFinished(
      const Step &step,
      bool success,
      const std::string &output) override {
    _latest_build_output.push_back(output);
  }

 private:
  std::vector<std::string> &_latest_build_output;
};

class FailingCommandRunner : public CommandRunner {
 public:
  void invoke(
      nt_string_view command,
      Step step,
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
      nt_string_view command,
      Step step,
      const Callback &callback) override {
    CHECK(_inner.size() < _max_capacity);
    _inner.invoke(command, step, callback);
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


CompiledManifest compileManifest(
    Path manifest_path, const RawManifest &raw_manifest) {
  // Globally leak memory for every invocation because it's the easiest thing.
  static std::vector<std::unique_ptr<flatbuffers::FlatBufferBuilder>> gBuilders;
  gBuilders.emplace_back(new flatbuffers::FlatBufferBuilder());
  auto &builder = *gBuilders.back();

  std::string err;
  CompiledManifest::compile(
      builder, manifest_path, raw_manifest, &err);
  err.clear();
  const auto maybe_manifest = CompiledManifest::load(
      string_view(
          reinterpret_cast<const char *>(builder.GetBufferPointer()),
          builder.GetSize()),
      &err);
  CHECK(err == "");
  CHECK(maybe_manifest);
  return *maybe_manifest;
}

std::vector<StepIndex> computeStepsToBuild(
    Paths &paths,
    const RawManifest &manifest,
    std::vector<StepIndex> &&specified_paths = {}) throw(BuildError) {
  return ::shk::detail::computeStepsToBuild(
      compileManifest(
          paths.get("build.ninja"),
          manifest),
      std::move(specified_paths));
}

std::vector<StepIndex> vec(const std::vector<StepIndex> &vec) {
  return vec;
}

Build computeBuild(
    Paths &paths,
    const RawManifest &manifest,
    size_t allowed_failures = 1) throw(BuildError) {
  auto compiled_manifest = compileManifest(paths.get("build.ninja"), manifest);
  return ::shk::detail::computeBuild(
      compiled_manifest,
      allowed_failures,
      ::shk::detail::computeStepsToBuild(compiled_manifest, {}));
}

void addOutput(
    Invocations &invocations,
    Invocations::Entry &entry,
    const std::string &path,
    const Fingerprint &fingerprint) {
  entry.output_files.push_back(invocations.fingerprints.size());
  invocations.fingerprints.emplace_back(path, fingerprint);
}

void addInput(
    Invocations &invocations,
    Invocations::Entry &entry,
    const std::string &path,
    const Fingerprint &fingerprint) {
  entry.input_files.push_back(invocations.fingerprints.size());
  invocations.fingerprints.emplace_back(path, fingerprint);
}

}  // anonymous namespace

TEST_CASE("Build") {
  time_t time = 555;
  const auto clock = [&time]() { return time; };
  InMemoryFileSystem fs(clock);
  Paths paths(fs);
  InMemoryInvocationLog log(fs, clock);
  Invocations invocations;
  RawManifest manifest;

  flatbuffers::FlatBufferBuilder empty_step_builder;
  auto empty = StepBuilder().build(empty_step_builder);

  RawStep single_output;
  single_output.command = "cmd";
  single_output.outputs = { paths.get("a") };

  RawStep single_output_b;
  single_output_b.command = "cmd";
  single_output_b.outputs = { paths.get("b") };

  RawStep multiple_outputs;
  multiple_outputs.command = "cmd";
  multiple_outputs.outputs = { paths.get("c"), paths.get("d") };

  RawStep single_input;
  single_input.command = "cmd";
  single_input.inputs = { paths.get("a") };

  RawStep single_implicit_input;
  single_implicit_input.command = "cmd";
  single_implicit_input.implicit_inputs = { paths.get("a") };

  RawStep single_dependency;
  single_dependency.command = "cmd";
  single_dependency.dependencies = { paths.get("a") };

  const auto parse = [&](const std::string &input) {
    fs.writeFile("build.ninja", input);
    return parseManifest(paths, fs, "build.ninja");
  };

  DummyCommandRunner dummy_runner(fs);

  std::vector<std::string> latest_build_output;

  const auto build_or_rebuild_manifest = [&](
      const std::string &manifest,
      size_t failures_allowed,
      CommandRunner &runner) {
    Paths paths(fs);
    fs.writeFile("build.ninja", manifest);

    return build(
        clock,
        fs,
        runner,
        [&latest_build_output](int total_steps) {
          return std::unique_ptr<BuildStatus>(
              new OutputCapturerBuildStatus(latest_build_output));
        },
        log,
        failures_allowed,
        {},
        compileManifest(
            paths.get("build.ninja"),
            parseManifest(paths, fs, "build.ninja")),
        log.invocations());
  };

  const auto build_manifest = [&](
      const std::string &manifest,
      size_t failures_allowed = 1) {
    return build_or_rebuild_manifest(manifest, failures_allowed, dummy_runner);
  };

  const auto to_compiled_manifest = [&](const RawManifest &raw_manifest) {
    return compileManifest(paths.get("build.ninj"), raw_manifest);
  };

  SECTION("interpretPath") {
    RawStep other_input;
    other_input.inputs = { paths.get("other") };
    other_input.outputs = { paths.get("foo") };

    RawStep multiple_outputs;
    multiple_outputs.inputs = { paths.get("hehe") };
    multiple_outputs.outputs = { paths.get("hej"), paths.get("there") };

    RawStep implicit_input;
    implicit_input.implicit_inputs = { paths.get("implicit_input") };
    implicit_input.outputs = { paths.get("implicit_output") };

    RawStep dependency;
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

    auto compiled_manifest = to_compiled_manifest(manifest);

    const auto &steps = manifest.steps;

    SECTION("normal (non-^)") {
      CHECK(
          steps[interpretPath(compiled_manifest, "a")].hash() ==
          single_output.hash());
      CHECK(
          steps[interpretPath(compiled_manifest, "b/../a")].hash() ==
          single_output.hash());
      CHECK_THROWS_AS(interpretPath(compiled_manifest, "x"), BuildError);
      CHECK_THROWS_AS(
          interpretPath(compiled_manifest, "other"), BuildError);
    }

    SECTION("^") {
      CHECK_THROWS_AS(
          interpretPath(compiled_manifest, "fancy_schmanzy^"),
          BuildError);
      CHECK(
          steps[interpretPath(compiled_manifest, "other^")].hash() ==
          other_input.hash());
      CHECK(  // No out edge
          steps[interpretPath(compiled_manifest, "a^")].hash() ==
          single_input.hash());
      CHECK(
          steps[interpretPath(compiled_manifest, "hehe^")].hash() ==
          multiple_outputs.hash());
      CHECK(
          steps[interpretPath(compiled_manifest, "implicit_input^")].hash() ==
          implicit_input.hash());
      CHECK(
          steps[interpretPath(compiled_manifest, "dependency_input^")].hash() ==
          dependency.hash());
    }

    SECTION("clean") {
      try {
        interpretPath(to_compiled_manifest(manifest), "clean");
        CHECK(!"Should throw");
      } catch (const BuildError &error) {
        CHECK(error.what() == std::string(
            "Unknown target 'clean', did you mean 'shk -t clean'?"));
      }
    }

    SECTION("help") {
      try {
        interpretPath(to_compiled_manifest(manifest), "help");
        CHECK(!"Should throw");
      } catch (const BuildError &error) {
        CHECK(error.what() == std::string(
            "Unknown target 'help', did you mean 'shk -h'?"));
      }
    }
  }

  SECTION("interpretPath") {
    SECTION("Empty") {
      CHECK(interpretPaths(
          to_compiled_manifest(manifest), 0, nullptr).empty());
    }

    SECTION("Paths") {
      manifest.steps = {
          single_output,
          single_output_b };

      std::string a = "a";
      std::string b = "b";
      char *in[] = { &a[0], &b[0] };
      const std::vector<Path> out = { paths.get("a"), paths.get("b") };
      CHECK(
          interpretPaths(
              to_compiled_manifest(manifest), 2, in) ==
          std::vector<StepIndex>({ 0, 1 }));
    }
  }

  SECTION("computeStepsToBuild helper") {
    manifest.steps = { single_output_b, multiple_outputs };

    // Kinda stupid test, yes I know. This is mostly just to get coverage, this
    // function is simple enough that I expect it to not have significant bugs.
    manifest.defaults = { paths.get("b") };

    auto compiled_manifest = to_compiled_manifest(manifest);
    CHECK(computeStepsToBuild(compiled_manifest, 0, nullptr) == vec({0}));
  }

  SECTION("computeStepsToBuild") {
    SECTION("trivial") {
      CHECK(computeStepsToBuild(paths, RawManifest()).empty());
    }

    SECTION("invalid defaults") {
      manifest.defaults = { paths.get("missing") };
      CHECK_THROWS_AS(computeStepsToBuild(paths, manifest), BuildError);
    }

    SECTION("defaults") {
      manifest.steps = { single_output_b, multiple_outputs };

      manifest.defaults = { paths.get("b") };
      CHECK(computeStepsToBuild(paths, manifest) == vec({0}));

      manifest.defaults = { paths.get("c") };
      CHECK(computeStepsToBuild(paths, manifest) == vec({1}));

      manifest.defaults = { paths.get("d") };
      CHECK(computeStepsToBuild(paths, manifest) == vec({1}));

      manifest.defaults = { paths.get("d"), paths.get("c") };
      // Duplicates are ok. We could deduplicate but that would just be an
      // unnecessary expense.
      CHECK(computeStepsToBuild(paths, manifest) == vec({1, 1}));

      manifest.defaults = { paths.get("b"), paths.get("c") };
      CHECK(computeStepsToBuild(paths, manifest) == vec({0, 1}));
    }

    SECTION("specified_outputs") {
      manifest.steps = { single_output_b, multiple_outputs };

      CHECK(computeStepsToBuild(paths, manifest, { 0 }) == vec({0}));
      CHECK(computeStepsToBuild(paths, manifest, { 1 }) == vec({1}));

      // Duplicates are ok. We could deduplicate but that would just be an
      // unnecessary expense.
      CHECK(computeStepsToBuild(paths, manifest, { 1, 1 }) ==
          vec({1, 1}));

      CHECK(computeStepsToBuild(paths, manifest, { 0, 1 }) ==
          vec({0, 1}));
    }

    SECTION("use root steps when defaults are missing") {
      manifest.steps = { single_output, single_input };
      CHECK(computeStepsToBuild(paths, manifest) == vec({1}));
    }
  }

  SECTION("computeBuild") {
    SECTION("empty") {
      const auto build = computeBuild(paths, RawManifest());
      CHECK(build.step_nodes.empty());
      CHECK(build.ready_steps.empty());
      CHECK(build.remaining_failures == 1);
    }

    SECTION("remaining_failures") {
      const auto build = computeBuild(paths, RawManifest(), 543);
      CHECK(build.remaining_failures == 543);
    }

    SECTION("ready_steps") {
      SECTION("basic") {
        manifest.steps = { single_output };
        CHECK(computeBuild(paths, manifest).ready_steps == vec({ 0 }));
      }

      SECTION("two steps") {
        manifest.steps = { single_output, single_output_b };
        CHECK(computeBuild(paths, manifest).ready_steps == vec({ 0, 1 }));
      }

      SECTION("single dep") {
        manifest.steps = { single_output, single_input };
        CHECK(computeBuild(paths, manifest).ready_steps == vec({ 0 }));

        manifest.steps = { single_input, single_output };
        CHECK(computeBuild(paths, manifest).ready_steps == vec({ 1 }));
      }

      SECTION("dep chain") {
        RawStep one;
        one.outputs = { paths.get("a") };
        RawStep two;
        two.inputs = { paths.get("a") };
        two.outputs = { paths.get("b") };
        RawStep three;
        three.inputs = { paths.get("b") };

        manifest.steps = { three, one, two };
        CHECK(computeBuild(paths, manifest).ready_steps == vec({ 1 }));

        manifest.steps = { one, two, three };
        CHECK(computeBuild(paths, manifest).ready_steps == vec({ 0 }));
      }

      SECTION("diamond dep") {
        RawStep one;
        one.outputs = { paths.get("a") };
        RawStep two_1;
        two_1.inputs = { paths.get("a") };
        two_1.outputs = { paths.get("b") };
        RawStep two_2;
        two_2.inputs = { paths.get("a") };
        two_2.outputs = { paths.get("c") };
        RawStep three;
        three.inputs = { paths.get("b"), paths.get("c") };

        manifest.steps = { three, one, two_1, two_2 };
        CHECK(computeBuild(paths, manifest).ready_steps == vec({ 1 }));

        manifest.steps = { three, two_2, two_1, one };
        CHECK(computeBuild(paths, manifest).ready_steps == vec({ 3 }));
      }
    }

    SECTION("step_nodes.should_build") {
      RawStep one;
      one.outputs = { paths.get("a") };
      RawStep two;
      two.inputs = { paths.get("a") };
      two.outputs = { paths.get("b") };
      RawStep three;
      three.inputs = { paths.get("b") };

      SECTION("everything") {
        manifest.steps = { one, two, three };
        const auto build = computeBuild(paths, manifest);
        REQUIRE(build.step_nodes.size() == 3);
        CHECK(build.step_nodes[0].should_build);
        CHECK(build.step_nodes[1].should_build);
        CHECK(build.step_nodes[2].should_build);
      }

      SECTION("just some") {
        manifest.steps = { one, two, three };
        manifest.defaults = { paths.get("b") };
        const auto build = computeBuild(paths, manifest);
        REQUIRE(build.step_nodes.size() == 3);
        CHECK(build.step_nodes[0].should_build);
        CHECK(build.step_nodes[1].should_build);
        CHECK(!build.step_nodes[2].should_build);
      }
    }

    SECTION("dependencies") {
      SECTION("independent") {
        manifest.steps = { single_output, single_output_b };
        const auto build = computeBuild(paths, manifest);
        REQUIRE(build.step_nodes.size() == 2);

        CHECK(build.step_nodes[0].dependencies == 0);
        CHECK(build.step_nodes[0].dependents == vec({}));

        CHECK(build.step_nodes[1].dependencies == 0);
        CHECK(build.step_nodes[1].dependents == vec({}));
      }

      SECTION("diamond") {
        RawStep one;
        one.outputs = { paths.get("a") };
        RawStep two_1;
        two_1.inputs = { paths.get("a") };
        two_1.outputs = { paths.get("b") };
        RawStep two_2;
        two_2.inputs = { paths.get("a") };
        two_2.outputs = { paths.get("c") };
        RawStep three;
        three.inputs = { paths.get("b"), paths.get("c") };

        manifest.steps = { three, two_2, two_1, one };
        const auto build = computeBuild(paths, manifest);
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

    SECTION("Dependency cycle") {
      RawStep one;
      one.outputs = { paths.get("a") };
      one.inputs = { paths.get("b") };
      RawStep two;
      two.inputs = { paths.get("a") };
      two.outputs = { paths.get("b") };

      RawManifest manifest;
      manifest.steps = { one, two };
      CHECK_THROWS_AS(computeBuild(paths, manifest), BuildError);
    }

    SECTION("Dependency cycle with specified target") {
      RawStep one;
      one.outputs = { paths.get("a") };
      one.inputs = { paths.get("b") };
      RawStep two;
      two.inputs = { paths.get("a") };
      two.outputs = { paths.get("b") };

      RawManifest manifest;
      // Need to specify a default, otherwise none of the steps are roots, and
      // nothing is "built".
      manifest.defaults = { paths.get("a") };
      manifest.steps = { one, two };
      CHECK_THROWS_AS(computeBuild(paths, manifest), BuildError);
    }
  }

  SECTION("isClean") {
    FingerprintMatchesMemo memo;

    SECTION("timestamp based (generator)") {
      const auto compile_manifest_step = [](const std::string &manifest_str) {
        InMemoryFileSystem fs;
        Paths paths(fs);
        fs.writeFile("build.ninja", manifest_str);

        const auto manifest = compileManifest(
            paths.get("build.ninja"),
            parseManifest(paths, fs, "build.ninja"));

        REQUIRE(manifest.steps().size() == 1);
        return manifest.steps()[0];
      };

      SECTION("no inputs") {
        auto step = compile_manifest_step(
            "rule my_rule\n"
            "  command = hi\n"
            "  generator = 1\n"
            "build out: my_rule\n");

        Invocations invocations;
        // Put nothing in invocations; it should not be needed

        CHECK(isClean(fs, log, memo, invocations, step));
      }

      SECTION("no outputs") {
        RawStep raw_step;
        raw_step.generator = true;
        raw_step.inputs = { paths.get("in") };
        RawManifest raw_manifest;
        raw_manifest.steps = { raw_step };

        const auto manifest = compileManifest(
            paths.get("build.ninja"), raw_manifest);

        REQUIRE(manifest.steps().size() == 1);
        auto step = manifest.steps()[0];

        Invocations invocations;
        // Put nothing in invocations; it should not be needed

        CHECK(isClean(fs, log, memo, invocations, step));
      }

      SECTION("missing input file") {
        fs.writeFile("out", "out");

        auto step = compile_manifest_step(
            "rule my_rule\n"
            "  command = hi\n"
            "  generator = 1\n"
            "build out: my_rule in\n");

        Invocations::Entry entry;
        Invocations invocations;
        // Pretend the step is clean via Invocations; it should not be used
        invocations.entries[step.hash()] = Invocations::Entry();

        CHECK(!isClean(fs, log, memo, invocations, step));
      }

      SECTION("missing output file") {
        fs.writeFile("in", "in");

        auto step = compile_manifest_step(
            "rule my_rule\n"
            "  command = hi\n"
            "  generator = 1\n"
            "build out: my_rule in\n");

        Invocations::Entry entry;
        Invocations invocations;
        // Pretend the step is clean via Invocations; it should not be used
        invocations.entries[step.hash()] = Invocations::Entry();

        CHECK(!isClean(fs, log, memo, invocations, step));
      }

      SECTION("input file newer") {
        fs.writeFile("out", "out");
        time++;
        fs.writeFile("in", "in");

        auto step = compile_manifest_step(
            "rule my_rule\n"
            "  command = hi\n"
            "  generator = 1\n"
            "build out: my_rule in\n");

        Invocations::Entry entry;
        Invocations invocations;
        // Pretend the step is clean via Invocations; it should not be used
        invocations.entries[step.hash()] = Invocations::Entry();

        CHECK(!isClean(fs, log, memo, invocations, step));
      }

      SECTION("single input file newer") {
        fs.writeFile("in1", "in1");
        fs.writeFile("out", "out");
        time++;
        fs.writeFile("in2", "in2");

        auto step = compile_manifest_step(
            "rule my_rule\n"
            "  command = hi\n"
            "  generator = 1\n"
            "build out: my_rule in1 in2\n");

        Invocations::Entry entry;
        Invocations invocations;
        // Pretend the step is clean via Invocations; it should not be used
        invocations.entries[step.hash()] = Invocations::Entry();

        CHECK(!isClean(fs, log, memo, invocations, step));
      }

      SECTION("single output file same as input") {
        fs.writeFile("out1", "out1");
        fs.writeFile("in2", "in2");
        time++;
        fs.writeFile("out2", "out2");

        auto step = compile_manifest_step(
            "rule my_rule\n"
            "  command = hi\n"
            "  generator = 1\n"
            "build out1 out2: my_rule in\n");

        Invocations::Entry entry;
        Invocations invocations;
        // Pretend the step is clean via Invocations; it should not be used
        invocations.entries[step.hash()] = Invocations::Entry();

        CHECK(!isClean(fs, log, memo, invocations, step));
      }

      SECTION("clean") {
        fs.writeFile("in", "in");
        time++;
        fs.writeFile("out", "out");

        auto step = compile_manifest_step(
            "rule my_rule\n"
            "  command = hi\n"
            "  generator = 1\n"
            "build out: my_rule in\n");

        Invocations invocations;
        // Put nothing in invocations; it should not be needed

        CHECK(isClean(fs, log, memo, invocations, step));
      }
    }

    SECTION("content based (non-generator)") {
      Hash hash_a;
      std::fill(hash_a.data.begin(), hash_a.data.end(), 123);
      Hash hash_b;
      std::fill(hash_a.data.begin(), hash_a.data.end(), 321);
      Hash hash_c;
      std::fill(hash_a.data.begin(), hash_a.data.end(), 0);

      fs.writeFile("one", "one_content");
      const auto one_fp = takeFingerprint(fs, clock() + 1, "one").first;
      const auto one_fp_racy = takeFingerprint(fs, clock(), "one").first;
      fs.writeFile("two", "two_content");
      const auto two_fp = takeFingerprint(fs, clock() + 1, "two").first;

      flatbuffers::FlatBufferBuilder step_with_hash_a_builder;
      auto step_with_hash_a = StepBuilder()
          .setHash(hash_a)
          .build(step_with_hash_a_builder);

      SECTION("no matching Invocation entry") {
        CHECK(!isClean(
            fs,
            log,
            memo,
            invocations,
            step_with_hash_a));
        CHECK(log.createdDirectories().empty());
        CHECK(log.entries().empty());
      }

      SECTION("no input or output files") {
        invocations.entries[hash_a] = Invocations::Entry();
        CHECK(isClean(
            fs,
            log,
            memo,
            invocations,
            step_with_hash_a));
        CHECK(log.createdDirectories().empty());
        CHECK(log.entries().empty());
      }

      SECTION("clean input") {
        Invocations::Entry entry;
        addInput(invocations, entry, "one", one_fp);
        invocations.entries[hash_a] = entry;
        memo.resize(invocations.fingerprints.size());
        CHECK(isClean(
            fs,
            log,
            memo,
            invocations,
            step_with_hash_a));
        CHECK(log.createdDirectories().empty());
        CHECK(log.entries().empty());
      }

      SECTION("dirty input") {
        Invocations::Entry entry;
        addInput(invocations, entry, "one", one_fp);
        invocations.entries[hash_a] = entry;
        fs.writeFile("one", "dirty");  // Make dirty
        memo.resize(invocations.fingerprints.size());
        CHECK(!isClean(
            fs,
            log,
            memo,
            invocations,
            step_with_hash_a));
        CHECK(log.createdDirectories().empty());
        CHECK(log.entries().empty());
      }

      SECTION("clean output") {
        Invocations::Entry entry;
        addOutput(invocations, entry, "one", one_fp);
        invocations.entries[hash_a] = entry;
        memo.resize(invocations.fingerprints.size());
        CHECK(isClean(
            fs,
            log,
            memo,
            invocations,
            step_with_hash_a));
        CHECK(log.createdDirectories().empty());
        CHECK(log.entries().empty());
      }

      SECTION("dirty output") {
        Invocations::Entry entry;
        addOutput(invocations, entry, "one", one_fp);
        invocations.entries[hash_a] = entry;
        fs.writeFile("one", "dirty");  // Make dirty
        memo.resize(invocations.fingerprints.size());
        CHECK(!isClean(
            fs,
            log,
            memo,
            invocations,
            step_with_hash_a));
        CHECK(log.createdDirectories().empty());
        CHECK(log.entries().empty());
      }

      SECTION("dirty input and output") {
        Invocations::Entry entry;
        addOutput(invocations, entry, "one", one_fp);
        addInput(invocations, entry, "two", two_fp);
        invocations.entries[hash_a] = entry;
        fs.writeFile("one", "dirty");
        fs.writeFile("two", "dirty!");
        memo.resize(invocations.fingerprints.size());
        CHECK(!isClean(
            fs,
            log,
            memo,
            invocations,
            step_with_hash_a));
        CHECK(log.createdDirectories().empty());
        CHECK(log.entries().empty());
      }

      SECTION("racily clean input") {
        Invocations::Entry entry;
        addInput(invocations, entry, "one", one_fp_racy);
        invocations.entries[hash_a] = entry;
        memo.resize(invocations.fingerprints.size());
        CHECK(isClean(
            fs,
            log,
            memo,
            invocations,
            step_with_hash_a));
        CHECK(log.createdDirectories().empty());
        REQUIRE(log.entries().count(hash_a) == 1);
        const auto &computed_entry = log.entries().find(hash_a)->second;
        REQUIRE(computed_entry.input_files.size() == 1);
        REQUIRE(computed_entry.input_files[0].first == "one");
        CHECK(computed_entry.output_files.empty());
      }

      SECTION("racily clean output") {
        Invocations::Entry entry;
        addOutput(invocations, entry, "one", one_fp_racy);
        invocations.entries[hash_a] = entry;
        memo.resize(invocations.fingerprints.size());
        CHECK(isClean(
            fs,
            log,
            memo,
            invocations,
            step_with_hash_a));
        CHECK(log.createdDirectories().empty());
        REQUIRE(log.entries().count(hash_a) == 1);
        const auto &computed_entry = log.entries().find(hash_a)->second;
        CHECK(computed_entry.input_files.empty());
        REQUIRE(computed_entry.output_files.size() == 1);
        REQUIRE(computed_entry.output_files[0].first == "one");
      }
    }
  }

  SECTION("computeCleanSteps") {
    SECTION("empty input") {
      CHECK(computeCleanSteps(
          clock,
          fs,
          log,
          invocations,
          to_compiled_manifest(manifest).steps(),
          Build()).empty());
    }

    SECTION("should compute clean steps") {
      manifest.steps = { single_output_b, multiple_outputs };
      // Add empty entry to mark clean
      invocations.entries[single_output_b.hash()];

      const auto build = computeBuild(paths, manifest);
      const auto clean_steps = computeCleanSteps(
          clock,
          fs,
          log,
          invocations,
          to_compiled_manifest(manifest).steps(),
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

      const auto build = computeBuild(paths, manifest);
      const auto clean_steps = computeCleanSteps(
          clock,
          fs,
          log,
          invocations,
          to_compiled_manifest(manifest).steps(),
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
        const RawManifest &manifest) {
      return computeCleanSteps(
          clock,
          fs,
          log,
          invocations,
          to_compiled_manifest(manifest).steps(),
          build);
    };

    SECTION("empty input") {
      Build build;
      CHECK(discardCleanSteps(
          to_compiled_manifest(manifest).steps(),
          CleanSteps(),
          build) == 0);
    }

    SECTION("all clean (independent)") {
      manifest.steps = { single_output_b, multiple_outputs };
      // Add empty entry to mark clean
      invocations.entries[single_output_b.hash()];
      invocations.entries[multiple_outputs.hash()];
      auto build = computeBuild(paths, manifest);
      CHECK(build.ready_steps.size() == 2);
      CHECK(discardCleanSteps(
          to_compiled_manifest(manifest).steps(),
          compute_clean_steps(build, invocations, manifest),
          build) == 2);
      CHECK(build.ready_steps.empty());
    }

    SECTION("all dirty") {
      manifest.steps = { single_output_b, multiple_outputs };
      auto build = computeBuild(paths, manifest);
      CHECK(build.ready_steps.size() == 2);
      CHECK(discardCleanSteps(
          to_compiled_manifest(manifest).steps(),
          compute_clean_steps(build, invocations, manifest),
          build) == 0);
      CHECK(build.ready_steps.size() == 2);
    }

    SECTION("some clean") {
      manifest.steps = { single_output_b, multiple_outputs };
      // Add empty entry to mark clean
      invocations.entries[single_output_b.hash()];
      auto build = computeBuild(paths, manifest);
      CHECK(build.ready_steps.size() == 2);
      CHECK(discardCleanSteps(
          to_compiled_manifest(manifest).steps(),
          compute_clean_steps(build, invocations, manifest),
          build) == 1);
      CHECK(build.ready_steps.size() == 1);
    }

    RawStep root;  // depends on the single_output step
    root.command = "cmd";
    root.inputs = { paths.get("a") };
    root.outputs = { paths.get("b") };

    RawStep phony;
    phony.outputs = { paths.get("a") };

    SECTION("phony step") {
      manifest.steps = { phony };
      auto build = computeBuild(paths, manifest);
      REQUIRE(build.ready_steps.size() == 1);
      CHECK(build.ready_steps[0] == 0);
      CHECK(discardCleanSteps(
          to_compiled_manifest(manifest).steps(),
          compute_clean_steps(build, invocations, manifest),
          build) == 1);
      CHECK(build.ready_steps.empty());
    }

    SECTION("all clean") {
      manifest.steps = { single_output, root };
      // Add empty entry to mark clean
      invocations.entries[single_output.hash()];
      addInput(
          invocations,
          invocations.entries[root.hash()],
          single_output.outputs[0].original(),
          Fingerprint());
      auto build = computeBuild(paths, manifest);
      CHECK(build.ready_steps.size() == 1);
      CHECK(discardCleanSteps(
          to_compiled_manifest(manifest).steps(),
          compute_clean_steps(build, invocations, manifest),
          build) == 2);
      CHECK(build.ready_steps.empty());
    }

    SECTION("leaf clean, root dirty") {
      manifest.steps = { single_output, root };
      // Add empty entry to mark clean
      invocations.entries[single_output.hash()];
      auto build = computeBuild(paths, manifest);
      REQUIRE(build.ready_steps.size() == 1);
      CHECK(build.ready_steps[0] == 0);
      CHECK(discardCleanSteps(
          to_compiled_manifest(manifest).steps(),
          compute_clean_steps(build, invocations, manifest),
          build) == 1);
      REQUIRE(build.ready_steps.size() == 1);
      CHECK(build.ready_steps[0] == 1);
    }

    SECTION("leaf dirty, root clean") {
      manifest.steps = { single_output, root };
      // Add empty entry to mark clean
      invocations.entries[single_input.hash()];
      auto build = computeBuild(paths, manifest);
      REQUIRE(build.ready_steps.size() == 1);
      CHECK(build.ready_steps[0] == 0);
      CHECK(discardCleanSteps(
          to_compiled_manifest(manifest).steps(),
          compute_clean_steps(build, invocations, manifest),
          build) == 0);
      REQUIRE(build.ready_steps.size() == 1);
      CHECK(build.ready_steps[0] == 0);
    }

    SECTION("leaf phony, root clean") {
      manifest.steps = { phony, root };
      // Add empty entry to mark clean
      invocations.entries[root.hash()];
      auto build = computeBuild(paths, manifest);
      REQUIRE(build.ready_steps.size() == 1);
      CHECK(build.ready_steps[0] == 0);
      CHECK(discardCleanSteps(
          to_compiled_manifest(manifest).steps(),
          compute_clean_steps(build, invocations, manifest),
          build) == 2);
      CHECK(build.ready_steps.empty());
    }
  }

  SECTION("deleteOldOutputs") {
    fs.writeFile("file", "contents");
    const auto fingerprint = takeFingerprint(fs, clock(), "file").first;
    fs.mkdir("dir_single_file");
    fs.writeFile("dir_single_file/file", "contents!");
    fs.mkdir("dir");
    fs.writeFile("dir/file2", "contents2");
    const auto fingerprint2 = takeFingerprint(fs, clock(), "dir/file2").first;
    fs.mkdir("dir/subdir");
    fs.writeFile("dir/subdir/file3", "contents3");
    const auto fingerprint3 =
        takeFingerprint(fs, clock(), "dir/subdir/file3").first;

    Invocations::Entry entry;
    Hash hash;
    std::fill(hash.data.begin(), hash.data.end(), 123);

    SECTION("missing step") {
      deleteOldOutputs(fs, Invocations(), log, Hash());
    }

    SECTION("empty step") {
      invocations.entries[hash];
      deleteOldOutputs(fs, invocations, log, hash);
    }

    SECTION("step with missing file") {
      fs.unlink("file");

      addOutput(invocations, entry, "file", fingerprint);

      invocations.entries[hash] = entry;
      deleteOldOutputs(fs, invocations, log, hash);
    }

    SECTION("don't delete inputs") {
      addInput(invocations, entry, "file", fingerprint);

      invocations.entries[hash] = entry;
      deleteOldOutputs(fs, invocations, log, hash);

      CHECK(fs.readFile("file") == "contents");
    }

    SECTION("delete output") {
      addInput(invocations, entry, "file", fingerprint);

      invocations.entries[hash] = entry;
      deleteOldOutputs(fs, invocations, log, hash);

      CHECK(fs.readFile("file") == "contents");
    }

    SECTION("delete output with mismatching fingerprint") {
      addInput(invocations, entry, "file", fingerprint2);

      invocations.entries[hash] = entry;
      deleteOldOutputs(fs, invocations, log, hash);

      CHECK(fs.readFile("file") == "contents");
    }

    SECTION("delete outputs") {
      addOutput(invocations, entry, "file", fingerprint);

      invocations.entries[hash] = entry;
      deleteOldOutputs(fs, invocations, log, hash);

      CHECK(fs.stat("file").result == ENOENT);
    }

    SECTION("delete created directory") {
      addOutput(
          invocations,
          entry,
          "dir_single_file/file",
          fingerprint2);
      invocations.entries[hash] = entry;

      invocations.created_directories.emplace(
          FileId(fs.lstat("dir_single_file")), "dir_single_file");

      deleteOldOutputs(fs, invocations, log, hash);

      CHECK(fs.stat("dir_single_file/file").result == ENOTDIR);
      CHECK(fs.stat("dir_single_file").result == ENOENT);
    }

    SECTION("delete created directories") {
      addOutput(invocations, entry, "dir/file2", fingerprint2);
      addOutput(
          invocations,
          entry,
          "dir/subdir/file3",
          fingerprint3);
      invocations.entries[hash] = entry;

      invocations.created_directories.emplace(
          FileId(fs.lstat("dir")), "dir");
      invocations.created_directories.emplace(
          FileId(fs.lstat("dir/subdir")), "dir/subdir");

      deleteOldOutputs(fs, invocations, log, hash);

      CHECK(fs.stat("dir/subdir/file3").result == ENOTDIR);
      CHECK(fs.stat("dir/subdir").result == ENOTDIR);
      CHECK(fs.stat("dir/file2").result == ENOTDIR);
      CHECK(fs.stat("dir").result == ENOENT);
    }

    SECTION("leave created directories that aren't empty") {
      addOutput(
          invocations,
          entry,
          "dir/subdir/file3",
          fingerprint3);
      invocations.entries[hash] = entry;

      invocations.created_directories.emplace(
          FileId(fs.lstat("dir")), "dir");
      invocations.created_directories.emplace(
          FileId(fs.lstat("dir/subdir")), "dir/subdir");

      deleteOldOutputs(fs, invocations, log, hash);

      CHECK(fs.stat("dir/subdir/file3").result == ENOTDIR);
      CHECK(fs.stat("dir/subdir").result == ENOENT);
      CHECK(fs.stat("dir").result != ENOENT);
    }

    SECTION("leave directories that weren't created by previous build") {
      addOutput(invocations, entry, "dir/file2", fingerprint2);
      addOutput(
          invocations,
          entry,
          "dir/subdir/file3",
          fingerprint3);
      invocations.entries[hash] = entry;

      deleteOldOutputs(fs, invocations, log, hash);

      CHECK(fs.stat("dir/subdir/file3").result == ENOENT);
      CHECK(fs.stat("dir/file2").result == ENOENT);
      CHECK(fs.stat("dir/subdir").result != ENOENT);
      CHECK(fs.stat("dir").result != ENOENT);
    }
  }

  SECTION("canSkipBuildCommand") {
    fs.writeFile("file", "contents");
    const auto file_fingerprint = takeFingerprint(
        fs, clock(), "file").first;
    const auto file_id = FileId(fs.lstat("file"));

    flatbuffers::FlatBufferBuilder fb_builder;

    SECTION("dirty step") {
      CHECK(!canSkipBuildCommand(
          fs,
          CleanSteps{ false },
          {},
          Invocations(),
          StepBuilder().build(fb_builder),
          0));
    }

    SECTION("no Invocations entry") {
      CHECK(!canSkipBuildCommand(
          fs,
          CleanSteps{ true },
          {},
          Invocations(),
          StepBuilder().build(fb_builder),
          0));
    }

    SECTION("no input files") {
      const auto step = StepBuilder().build(fb_builder);
      Invocations invocations;
      invocations.entries[step.hash()] = Invocations::Entry();

      CHECK(canSkipBuildCommand(
          fs,
          CleanSteps{ true },
          {},
          invocations,
          step,
          0));
    }

    SECTION("input file that has not been written") {
      const auto step = StepBuilder().build(fb_builder);

      Invocations::Entry entry;
      entry.input_files = { 0 };

      Invocations invocations;
      invocations.fingerprints.emplace_back(
          "file", file_fingerprint);
      invocations.entries[step.hash()] = entry;

      CHECK(canSkipBuildCommand(
          fs,
          CleanSteps{ true },
          {},
          invocations,
          step,
          0));
    }

    SECTION("input file that has been written but is clean") {
      const auto step = StepBuilder().build(fb_builder);

      Invocations::Entry entry;
      entry.input_files = { 0 };

      Invocations invocations;
      invocations.fingerprints.emplace_back(
          "file", file_fingerprint);
      invocations.entries[step.hash()] = entry;

      CHECK(canSkipBuildCommand(
          fs,
          CleanSteps{ true },
          { { file_id, file_fingerprint.hash } },
          invocations,
          step,
          0));
    }

    SECTION("input file that has been overwritten") {
      const auto step = StepBuilder().build(fb_builder);

      Invocations::Entry entry;
      entry.input_files = { 0 };

      Invocations invocations;
      invocations.fingerprints.emplace_back(
          "file", file_fingerprint);
      invocations.entries[step.hash()] = entry;

      Hash different_hash = file_fingerprint.hash;
      different_hash.data[0]++;

      CHECK(!canSkipBuildCommand(
          fs,
          CleanSteps{ true },
          { { file_id, different_hash } },
          invocations,
          step,
          0));
    }

    SECTION("output file that has been overwritten") {
      const auto step = StepBuilder().build(fb_builder);

      Invocations::Entry entry;
      entry.output_files = { 0 };

      Invocations invocations;
      invocations.fingerprints.emplace_back(
          "file", file_fingerprint);
      invocations.entries[step.hash()] = entry;

      Hash different_hash = file_fingerprint.hash;
      different_hash.data[0]++;

      CHECK(canSkipBuildCommand(
          fs,
          CleanSteps{ true },
          { { file_id, different_hash } },
          invocations,
          step,
          0));
    }
  }

  SECTION("countStepsToBuild") {
    // TODO(peck): Test this
  }

  SECTION("deleteStaleOutputs") {
    const auto delete_stale_outputs = [&](
        const std::string &manifest) {
      const auto compiled_manifest =
          to_compiled_manifest(parse(manifest));

      deleteStaleOutputs(
          fs,
          log,
          compiled_manifest.steps(),
          log.invocations());
    };

    SECTION("delete stale outputs") {
      const auto cmd = dummy_runner.constructCommand({}, {"out"});
      const auto manifest =
          "rule cmd\n"
          "  command = " + cmd + "\n"
          "build out: cmd\n";
      CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
      dummy_runner.checkCommand(fs, cmd);

      const auto cmd2 = dummy_runner.constructCommand({}, {"out2"});
      const auto manifest2 =
          "rule cmd2\n"
          "  command = " + cmd2 + "\n"
          "build out2: cmd2\n";
      delete_stale_outputs(manifest2);
      CHECK_THROWS(dummy_runner.checkCommand(fs, cmd));
      CHECK_THROWS(dummy_runner.checkCommand(fs, cmd2));
    }

    SECTION("delete stale outputs and their directories") {
      const auto cmd = dummy_runner.constructCommand({}, {"dir/out"});
      const auto manifest =
          "rule cmd\n"
          "  command = " + cmd + "\n"
          "build dir/out: cmd\n";
      CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
      dummy_runner.checkCommand(fs, cmd);
      CHECK(S_ISDIR(fs.stat("dir").metadata.mode));
      CHECK(fs.stat("dir2").result == ENOENT);

      const auto cmd2 = dummy_runner.constructCommand({}, {"dir2/out2"});
      const auto manifest2 =
          "rule cmd2\n"
          "  command = " + cmd2 + "\n"
          "build dir2/out2: cmd2\n";
      delete_stale_outputs(manifest2);
      CHECK_THROWS(dummy_runner.checkCommand(fs, cmd));
      CHECK_THROWS(dummy_runner.checkCommand(fs, cmd2));
      CHECK(fs.stat("dir").result == ENOENT);
      CHECK(fs.stat("dir2").result == ENOENT);
    }

    SECTION("delete outputs of removed step") {
      const auto cmd = dummy_runner.constructCommand({}, {"out"});
      const auto manifest =
          "rule cmd\n"
          "  command = " + cmd + "\n"
          "build out: cmd\n";
      CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
      dummy_runner.checkCommand(fs, cmd);

      const auto manifest2 = "";
      delete_stale_outputs(manifest2);
      CHECK_THROWS(dummy_runner.checkCommand(fs, cmd));
    }
  }

  SECTION("build") {
    const auto verify_noop_build = [&](
        const std::string &manifest,
        size_t failures_allowed = 1) {
      FailingCommandRunner failing_runner;
      CHECK(build_or_rebuild_manifest(
          manifest,
          failures_allowed,
          failing_runner) == BuildResult::NO_WORK_TO_DO);
    };

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

      SECTION("two steps overwriting each other's outputs") {
        const auto cmd1 = dummy_runner.constructCommand({}, {"out"});
        const auto cmd2 = dummy_runner.constructCommand({}, {"out"});
        const auto manifest =
            "rule cmd1\n"
            "  command = " + cmd1 + "\n"
            "rule cmd2\n"
            "  command = " + cmd2 + "\n"
            "build cmd1: cmd1\n"
            "build cmd2: cmd2\n";
        CHECK(build_manifest(manifest) == BuildResult::FAILURE);

        REQUIRE(latest_build_output.size() == 2);
        CHECK(latest_build_output[0] == "");
        CHECK(
            latest_build_output[1] ==
            "shk: Build step wrote to file that other build step has already "
            "written to: out\n");
      }

      SECTION("two steps with depfile") {
        const auto cmd1 = dummy_runner.constructCommand({}, {"dep1"});
        const auto cmd2 = dummy_runner.constructCommand({}, {"dep2"});
        const auto manifest =
            "rule cmd1\n"
            "  command = " + cmd1 + "\n"
            "  depfile = dep1\n"
            "  deps = gcc\n"
            "rule cmd2\n"
            "  command = " + cmd2 + "\n"
            "  depfile = dep2\n"
            "  deps = gcc\n"
            "build cmd1: cmd1\n"
            "build cmd2: cmd2\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
      }

      SECTION("create directory for output") {
        const auto cmd = dummy_runner.constructCommand({}, {"dir/out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build dir/out: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        CHECK(S_ISDIR(fs.stat("dir").metadata.mode));
        CHECK(log.createdDirectories().count("dir") == 1);
        dummy_runner.checkCommand(fs, cmd);
      }

      SECTION("create directories for output") {
        const auto cmd = dummy_runner.constructCommand({}, {"dir/inner/out"});
        const auto manifest =
            "rule cmd\n"
            "  command = " + cmd + "\n"
            "build dir/inner/out: cmd\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        CHECK(S_ISDIR(fs.stat("dir").metadata.mode));
        CHECK(S_ISDIR(fs.stat("dir/inner").metadata.mode));
        CHECK(log.createdDirectories().count("dir") == 1);
        CHECK(log.createdDirectories().count("dir/inner") == 1);
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

#if 0  // TODO(peck): Make this test work
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
#endif

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

      SECTION("just phony rule counts as no-op") {
        const auto manifest =
            "build one: phony\n";
        CHECK(build_manifest(manifest) == BuildResult::NO_WORK_TO_DO);
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
        CHECK(build_manifest(manifest) == BuildResult::NO_WORK_TO_DO);
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

      SECTION("bypass commands (restat)") {
        // It should not rebuild steps where all inputs and ouputs were clean,
        // even if steps that depend on them had to be rebuilt.

        const auto cmd1 = dummy_runner.constructCommand({}, { "out1" });
        const auto cmd2 = dummy_runner.constructCommand({ "out1" }, { "out2" });
        const auto cmd3 = dummy_runner.constructCommand({ "out2" }, { "out3" });
        const auto manifest =
            "rule cmd1\n"
            "  command = " + cmd1 + "\n"
            "rule cmd2\n"
            "  command = " + cmd2 + "\n"
            "rule cmd3\n"
            "  command = " + cmd3 + "\n"
            "build out1: cmd1\n"
            "build out2: cmd2 out1\n"
            "build out3: cmd3 out2\n";
        CHECK(latest_build_output.size() == 0);
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        CHECK(dummy_runner.getCommandsRun() == 3);
        CHECK(latest_build_output.size() == 3);
        fs.unlink("out1");
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd1);
        dummy_runner.checkCommand(fs, cmd2);
        dummy_runner.checkCommand(fs, cmd3);

        // Should be 3 commands run from the initial run, and then 2 from the
        // second one. The second time, cmd3 should not have been invoked.
        CHECK(dummy_runner.getCommandsRun() == 5);

        // Should have reported 3 finished build steps to the BuildStatus during
        // the build.
        CHECK(latest_build_output.size() == 3);
      }

      SECTION("bypass commands (restat) in longer chain") {
        // This test is primarily for ensuring that the build continues
        // (that markStepNodeAsDone is called) even for steps that are bypassed.

        const auto cmd1 = dummy_runner.constructCommand({}, { "out1" });
        const auto cmd2 = dummy_runner.constructCommand({ "out1" }, { "out2" });
        const auto cmd3 = dummy_runner.constructCommand({ "out2" }, { "out3" });
        const auto cmd4 = dummy_runner.constructCommand({ "out3" }, { "out4" });
        const auto manifest =
            "rule cmd1\n"
            "  command = " + cmd1 + "\n"
            "rule cmd2\n"
            "  command = " + cmd2 + "\n"
            "rule cmd3\n"
            "  command = " + cmd3 + "\n"
            "rule cmd4\n"
            "  command = " + cmd4 + "\n"
            "build out1: cmd1\n"
            "build out2: cmd2 out1\n"
            "build out3: cmd3 out2\n"
            "build out4: cmd4 out3\n";
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        CHECK(dummy_runner.getCommandsRun() == 4);
        CHECK(latest_build_output.size() == 4);
        fs.unlink("out1");
        fs.unlink("out4");
        CHECK(build_manifest(manifest) == BuildResult::SUCCESS);
        dummy_runner.checkCommand(fs, cmd1);
        dummy_runner.checkCommand(fs, cmd2);
        dummy_runner.checkCommand(fs, cmd3);
        dummy_runner.checkCommand(fs, cmd4);

        // Should be 3 commands run from the initial run, and then 2 from the
        // second one. The second time, cmd3 should not have been invoked.
        CHECK(dummy_runner.getCommandsRun() == 7);

        // Should have reported 4 finished build steps to the BuildStatus during
        // the build.
        CHECK(latest_build_output.size() == 4);
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
