#include <catch.hpp>
#include <rapidcheck/catch.h>

TEST_CASE("Correctness") {
  rc::prop("build steps are performed", []() {
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
