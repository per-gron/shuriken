#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "build.h"
#include "build_status.h"
#include "path.h"
#include "step.h"

#include "dummy_command_runner.h"
#include "generators.h"
#include "in_memory_file_system.h"
#include "in_memory_invocation_log.h"

namespace shk {

using Files = std::unordered_map<shk::Path, std::string>;

struct BuildInput {
  std::vector<Step> steps;
  Files input_files;
};

void showValue(const Step &step, std::ostream &os) {
  os << "<Step:'" << step.command << "' ";
  if (step.restat) {
    os << "restat ";
  }
  os << rc::toString(step.dependencies) << " => " << rc::toString(step.outputs);
  os << ">";
}

void showValue(const BuildInput &build_input, std::ostream &os) {
  os << "  BuildInput:\n";
  os << "steps: " << rc::toString(build_input.steps) << "\n";
  os << "input_files: " << rc::toString(build_input.input_files) << std::endl;
}

namespace gen {

/**
 * Partially generates a build step. Used by the steps generator, whichadds
 * more information to construct a real DAG.
 */
rc::Gen<Step> step(const std::shared_ptr<Paths> &paths) {
  return rc::gen::exec([paths] {
    Step step;
    // step.command is generated later
    step.restat = *rc::gen::arbitrary<bool>();
    step.dependencies = *pathVector(paths);
    step.outputs = *pathVector(paths);
    return step;
  });
}

/**
 * Construct a list of Step objects and input files that represent an arbitrary
 * valid Shuriken build.
 */
rc::Gen<BuildInput> buildInput(const std::shared_ptr<Paths> &paths) {
  return rc::gen::exec([paths] {
    BuildInput build_input;

    build_input.steps =
        *rc::gen::container<std::vector<Step>>(step(paths));

    for (const auto &step : build_input.steps) {
      for (const auto &path : step.dependencies) {
        build_input.input_files[path] = *rc::gen::arbitrary<std::string>();
      }
    }

    // TODO(peck): Generate dependencies between steps
    // TODO(peck): Generate commands for steps

    return build_input;
  });
}

}  // namespace gen

void addFilesToFileSystem(const Files &files, FileSystem &file_system) {
  for (const auto &file : files) {
    mkdirsFor(file_system, file.first.original());
    file_system.writeFile(file.first.original(), file.second);
  }
}

TEST_CASE("Correctness") {
#if 0  // TODO(peck): This test is currently broken/not fully implemented
  rc::prop("successful builds should run all build steps", []() {
    const auto paths = std::make_shared<Paths>();

    BuildInput build_input = *gen::buildInput(paths);

    InMemoryFileSystem file_system(*paths);
    addFilesToFileSystem(build_input.input_files, file_system);

    DummyCommandRunner command_runner(file_system);

    BuildStatus build_status;

    InMemoryInvocationLog invocation_log;

    build(
        file_system,
        command_runner,
        build_status,
        invocation_log,
        build_input.steps,
        Invocations() /* No prior invocations, this is a build from scratch */);
  });
#endif

  rc::prop("In {build, build}, the second build is a no-op", []() {
    // TODO(peck): Implement me
  });

  rc::prop("{build, change, build} is same as {change, build}", []() {
    // TODO(peck): Implement me
  });

  rc::prop("{build, change, build, undo, build} is same as {build}", []() {
    // TODO(peck): Implement me
  });

  rc::prop("clean", []() {
    // TODO(peck): Implement me
  });

  rc::prop("build steps that fail should not leave any trace", []() {
    // TODO(peck): Implement me
  });

  rc::prop("mid-build termination", []() {
    // TODO(peck): Implement me
  });
}

TEST_CASE("Efficiency") {
  rc::prop("second build is no-op", []() {
    // TODO(peck): Implement me
  });

  rc::prop("minimal rebuilds", []() {
    // TODO(peck): Implement me
  });

  rc::prop("restat", []() {
    // TODO(peck): Implement me
  });

  rc::prop("parallelism", []() {
    // TODO(peck): Implement me
  });
}

TEST_CASE("Error detection") {
  rc::prop("detect insufficiently declared dependencies", []() {
    // TODO(peck): Implement me
  });

  rc::prop("detect read of output", []() {
    // TODO(peck): Implement me
  });

  rc::prop("detect write to input", []() {
    // TODO(peck): Implement me
  });

  rc::prop("detect failure to write declared outputs", []() {
    // TODO(peck): Implement me
  });

  rc::prop("detect access network", []() {
    // TODO(peck): Implement me

    // Maybe just a unit test for this one?
  });

  rc::prop("detect spawn daemon", []() {
    // TODO(peck): Implement me
  });

  rc::prop("detect cyclic dependencies", []() {
    // TODO(peck): Implement me
  });

  rc::prop("restrict environment variables", []() {
    // TODO(peck): Implement me

    // Move this to CommandRunner tests?
  });
}

}  // namespace shk
