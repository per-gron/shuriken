#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "path.h"

namespace shk {
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

using Files = std::unordered_map<shk::Path, std::string>;

rc::Gen<Files> files(Paths &paths) {
  return rc::gen::exec([&paths] {
    return *rc::gen::container<Files>(
        path(paths),
        rc::gen::arbitrary<std::string>());
  });
}

}  // namespace gen

TEST_CASE("Correctness") {
  rc::prop("successful builds should create declared output files", []() {
    Paths paths;
    printf("%s\n", (*gen::path(paths)).canonicalized().c_str());
    // TODO(peck): Implement me
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
