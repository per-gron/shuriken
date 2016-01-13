#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "build.h"
#include "build_status.h"
#include "path.h"
#include "step.h"

#include "dummy_command_runner.h"
#include "in_memory_file_system.h"
#include "in_memory_invocation_log.h"

namespace shk {

using Files = std::unordered_map<shk::Path, std::string>;

struct BuildInput {
  std::vector<Step> steps;
  Files input_files;
};

namespace gen {

rc::Gen<shk::Path> path(Paths &paths) {
  return rc::gen::exec([&paths] {
    const auto path_component_gen =
        rc::gen::container<std::string>(rc::gen::inRange<char>('a', 'z'));
    const auto path_components =
        *rc::gen::container<std::vector<std::string>>(path_component_gen);
    std::string path;
    for (const auto &path_component : path_components) {
      if (!path.empty()) {
        path += "/";
      }
      path += path_component;
    }
    return paths.get(path);
  });
}

rc::Gen<std::vector<Path>> pathVector(Paths &paths) {
  return rc::gen::container<std::vector<Path>>(path(paths));
}

/**
 * Partially generates a build step. Used by the steps generator, whichadds
 * more information to construct a real DAG.
 */
rc::Gen<Step> step(Paths &paths) {
  return rc::gen::exec([&paths] {
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
rc::Gen<BuildInput> buildInput(Paths &paths) {
  return rc::gen::exec([&paths] {
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
  // TODO(peck)
}

TEST_CASE("Correctness") {
  rc::prop("successful builds should create declared output files", []() {
    Paths paths;

    BuildInput build_input = *gen::buildInput(paths);

    InMemoryFileSystem file_system;
    addFilesToFileSystem(build_input.input_files, file_system);

    DummyCommandRunner command_runner;

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

  rc::prop("build steps that fail should not leave any trace", []() {
    // TODO(peck): Implement me
  });

  rc::prop("build, change, build is same as change, build", []() {
    // TODO(peck): Implement me
  });

  rc::prop("build, change, build, undo, build is same as build", []() {
    // TODO(peck): Implement me
  });

  rc::prop("clean", []() {
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
