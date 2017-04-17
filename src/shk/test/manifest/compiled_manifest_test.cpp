#include <catch.hpp>

#include "manifest/compiled_manifest.h"

#include "../in_memory_file_system.h"
#include "step_builder.h"

namespace shk {
namespace detail {
namespace {

template <typename Container, typename Value>
bool contains(const Container &container, const Value &value) {
  return std::find(
      container.begin(), container.end(), value) != container.end();
}

template <typename View>
std::vector<typename View::value_type> toVector(View view) {
  auto ans = std::vector<typename View::value_type>();
  ans.reserve(view.size());
  std::copy(view.begin(), view.end(), std::back_inserter(ans));
  return ans;
}

}  // anonymous namespace

TEST_CASE("CompiledManifest") {
  InMemoryFileSystem fs;
  Paths paths(fs);
  RawManifest manifest;

  const auto manifest_path = paths.get("b.ninja");

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

  SECTION("computeOutputPathMap") {
    SECTION("basics") {
      CHECK(computeOutputPathMap(std::vector<RawStep>()).empty());
      CHECK(computeOutputPathMap({ empty }).empty());
      CHECK(computeOutputPathMap({ single_input }).empty());
      CHECK(computeOutputPathMap({ single_implicit_input }).empty());
      CHECK(computeOutputPathMap({ single_dependency }).empty());
    }

    SECTION("single output") {
      const auto map = computeOutputPathMap({ single_output });
      CHECK(map.size() == 1);
      const auto it = map.find(paths.get("a"));
      REQUIRE(it != map.end());
      CHECK(it->second == 0);
    }

    SECTION("multiple outputs") {
      auto map = computeOutputPathMap({
          single_output, single_output_b, multiple_outputs });
      CHECK(map.size() == 4);
      CHECK(map[paths.get("a")] == 0);
      CHECK(map[paths.get("b")] == 1);
      CHECK(map[paths.get("c")] == 2);
      CHECK(map[paths.get("d")] == 2);
    }

    SECTION("duplicate outputs") {
      CHECK_THROWS_AS(
          computeOutputPathMap({ single_output, single_output }), BuildError);
    }
  }

  SECTION("cycleErrorMessage") {
    CHECK(
        cycleErrorMessage({}) == "[internal error]");
    CHECK(
        cycleErrorMessage({ paths.get("a") }) == "a -> a");
    CHECK(
        cycleErrorMessage({ paths.get("a"), paths.get("b") }) == "a -> b -> a");
  }

  SECTION("Constructor") {
    SECTION("basics") {
      RawManifest manifest;
      manifest.steps = { single_output };

      CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

      REQUIRE(compiled_manifest.steps().size() == 1);
      CHECK(compiled_manifest.steps()[0].hash() == single_output.hash());
    }

    SECTION("inputs") {
      SECTION("empty") {
        RawManifest manifest;
        manifest.steps = { single_output };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

        CHECK(compiled_manifest.inputs().empty());
      }

      SECTION("inputs") {
        RawManifest manifest;
        manifest.steps = { single_input };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

        CHECK(
            compiled_manifest.inputs() ==
            PathToStepList({ { "a", 0 } }));
      }

      SECTION("implicit_inputs") {
        RawManifest manifest;
        manifest.steps = { single_implicit_input };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

        CHECK(
            compiled_manifest.inputs() ==
            PathToStepList({ { "a", 0 } }));
      }

      SECTION("dependencies") {
        RawManifest manifest;
        manifest.steps = { single_dependency };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

        CHECK(
            compiled_manifest.inputs() ==
            PathToStepList({ { "a", 0 } }));
      }

      SECTION("shared inputs") {
        RawManifest manifest;
        manifest.steps = { single_dependency, single_input };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

        CHECK(
            compiled_manifest.inputs() ==
            PathToStepList({ { "a", 0 } }));
      }

      SECTION("different inputs") {
        for (int swap_steps = 0; swap_steps <= 1; swap_steps++) {
          RawStep single_input_b;
          single_input_b.inputs = { paths.get("b") };

          RawManifest manifest;
          if (swap_steps) {
            manifest.steps = { single_input_b, single_dependency };
          } else {
            manifest.steps = { single_dependency, single_input_b };
          }

          CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

          CHECK(
              compiled_manifest.inputs() ==
              PathToStepList({ { "a", swap_steps }, { "b", !swap_steps } }));
        }
      }

      SECTION("canonicalize path") {
        RawStep step;
        step.inputs = { paths.get("a/../b") };

        RawManifest manifest;
        manifest.steps = { step };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

        CHECK(
            compiled_manifest.inputs() ==
            PathToStepList({ { "b", 0 } }));
      }
    }

    SECTION("outputs") {
      SECTION("empty") {
        RawManifest manifest;
        manifest.steps = { single_input };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

        CHECK(compiled_manifest.outputs().empty());
      }

      SECTION("outputs") {
        RawManifest manifest;
        manifest.steps = { single_output };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

        CHECK(
            compiled_manifest.outputs() ==
            PathToStepList({ { "a", 0 } }));
      }

      SECTION("different outputs") {
        for (int swap_steps = 0; swap_steps <= 1; swap_steps++) {
          RawManifest manifest;
          if (swap_steps) {
            manifest.steps = { single_output_b, single_output };
          } else {
            manifest.steps = { single_output, single_output_b };
          }

          CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

          CHECK(
              compiled_manifest.outputs() ==
              PathToStepList({ { "a", swap_steps }, { "b", !swap_steps } }));
        }
      }

      SECTION("canonicalize path") {
        RawStep step;
        step.outputs = { paths.get("a/../b") };

        RawManifest manifest;
        manifest.steps = { step };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

        CHECK(
            compiled_manifest.outputs() ==
            PathToStepList({ { "b", 0 } }));
      }
    }

    SECTION("input without corresponding step") {
      RawManifest manifest;
      manifest.steps = { single_input };

      CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
      REQUIRE(compiled_manifest.steps().size() == 1);
      CHECK(
          toVector(compiled_manifest.steps()[0].dependencies()) ==
          std::vector<StepIndex>{});
    }

    SECTION("inputs") {
      RawManifest manifest;
      manifest.steps = { single_output, single_input };

      CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
      REQUIRE(compiled_manifest.steps().size() == 2);
      CHECK(
          toVector(compiled_manifest.steps()[0].dependencies()) ==
          std::vector<StepIndex>{});
      CHECK(
          toVector(compiled_manifest.steps()[1].dependencies()) ==
          std::vector<StepIndex>{ 0 });
    }

    SECTION("implicit inputs") {
      RawManifest manifest;
      manifest.steps = { single_output, single_implicit_input };

      CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
      REQUIRE(compiled_manifest.steps().size() == 2);
      CHECK(
          toVector(compiled_manifest.steps()[0].dependencies()) ==
          std::vector<StepIndex>{});
      CHECK(
          toVector(compiled_manifest.steps()[1].dependencies()) ==
          std::vector<StepIndex>{ 0 });
    }

    SECTION("dependencies") {
      RawManifest manifest;
      manifest.steps = { single_output, single_dependency };

      CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
      REQUIRE(compiled_manifest.steps().size() == 2);
      CHECK(
          toVector(compiled_manifest.steps()[0].dependencies()) ==
          std::vector<StepIndex>{});
      CHECK(
          toVector(compiled_manifest.steps()[1].dependencies()) ==
          std::vector<StepIndex>{ 0 });
    }

    SECTION("defaults") {
      SECTION("empty") {
        RawManifest manifest;
        manifest.steps = { single_output };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        CHECK(compiled_manifest.defaults().empty());
      }

      SECTION("one") {
        RawManifest manifest;
        manifest.steps = { single_output, single_output_b };
        manifest.defaults = { paths.get("b") };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        REQUIRE(compiled_manifest.defaults().size() == 1);
        CHECK(compiled_manifest.defaults()[0] == 1);
      }

      SECTION("two") {
        RawManifest manifest;
        manifest.steps = { single_output, single_output_b };
        manifest.defaults = { paths.get("b"), paths.get("a") };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        REQUIRE(compiled_manifest.defaults().size() == 2);
        CHECK(compiled_manifest.defaults()[0] == 1);
        CHECK(compiled_manifest.defaults()[1] == 0);
      }
    }

    SECTION("roots") {
      SECTION("empty") {
        RawManifest manifest;
        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        CHECK(compiled_manifest.roots().empty());
      }

      SECTION("single empty step") {
        RawManifest manifest;
        manifest.steps = { empty };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        CHECK(
            toVector(compiled_manifest.roots()) ==
            std::vector<StepIndex>{ 0 });
      }

      SECTION("two empty steps") {
        RawManifest manifest;
        manifest.steps = { empty, empty };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        CHECK(
            toVector(compiled_manifest.roots()) ==
            std::vector<StepIndex>({ 0, 1 }));
      }

      SECTION("one step depending on another") {
        RawManifest manifest;
        manifest.steps = { single_output, single_input };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        CHECK(
            toVector(compiled_manifest.roots()) ==
            std::vector<StepIndex>{ 1 });
      }

      SECTION("one step depending on another, reverse order") {
        RawManifest manifest;
        manifest.steps = { single_input, single_output };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        CHECK(
            toVector(compiled_manifest.roots()) ==
            std::vector<StepIndex>{ 0 });
      }

      SECTION("one step depending on another plus independent step") {
        RawManifest manifest;
        manifest.steps = { single_output, single_input, empty };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        CHECK(
            toVector(compiled_manifest.roots()) ==
            std::vector<StepIndex>({ 1, 2 }));
      }

      SECTION("cycle") {
        RawStep cyclic_step_1;
        cyclic_step_1.outputs = { paths.get("b") };
        cyclic_step_1.inputs = { paths.get("a") };
        RawStep cyclic_step_2;
        cyclic_step_2.outputs = { paths.get("a") };
        cyclic_step_2.inputs = { paths.get("b") };

        RawManifest manifest;
        manifest.steps = { cyclic_step_1, cyclic_step_2 };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        CHECK(
            toVector(compiled_manifest.roots()) ==
            std::vector<StepIndex>{});
      }
    }

    SECTION("build_dir") {
      RawManifest manifest;
      manifest.build_dir = "hello";

      CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
      CHECK(compiled_manifest.buildDir() == "hello");
    }

    SECTION("output dirs") {
      SECTION("current working directory") {
        RawStep step;
        step.outputs = { paths.get("a") };

        RawManifest manifest;
        manifest.steps = { step };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].outputDirs().size() == 0);
      }

      SECTION("one directory") {
        RawStep step;
        step.outputs = { paths.get("dir/a") };

        RawManifest manifest;
        manifest.steps = { step };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].outputDirs().size() == 1);
        CHECK(contains(compiled_manifest.steps()[0].outputDirs(), "dir"));
      }

      SECTION("two stesps") {
        RawStep step1;
        step1.outputs = { paths.get("dir1/a") };
        RawStep step2;
        step2.outputs = { paths.get("dir2/a") };

        RawManifest manifest;
        manifest.steps = { step1, step2 };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        REQUIRE(compiled_manifest.steps().size() == 2);
        CHECK(compiled_manifest.steps()[0].outputDirs().size() == 1);
        CHECK(contains(compiled_manifest.steps()[0].outputDirs(), "dir1"));
        CHECK(compiled_manifest.steps()[1].outputDirs().size() == 1);
        CHECK(contains(compiled_manifest.steps()[1].outputDirs(), "dir2"));
      }

      SECTION("two directories") {
        RawStep step;
        step.outputs = { paths.get("dir1/a"), paths.get("dir2/a") };

        RawManifest manifest;
        manifest.steps = { step };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].outputDirs().size() == 2);
        CHECK(contains(compiled_manifest.steps()[0].outputDirs(), "dir1"));
        CHECK(contains(compiled_manifest.steps()[0].outputDirs(), "dir2"));
      }

      SECTION("duplicate directories") {
        RawStep step;
        step.outputs = { paths.get("dir/a"), paths.get("dir/b") };

        RawManifest manifest;
        manifest.steps = { step };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].outputDirs().size() == 1);
        CHECK(contains(compiled_manifest.steps()[0].outputDirs(), "dir"));
      }
    }

    SECTION("manifest_step") {
      SECTION("present") {
        RawStep step;
        step.outputs = { manifest_path };

        RawManifest manifest;
        manifest.steps = { single_input, step };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

        CHECK(compiled_manifest.manifestStep() == 1);
      }

      SECTION("missing") {
        RawManifest manifest;
        manifest.steps = { single_input };

        CompiledManifest compiled_manifest(manifest_path, std::move(manifest));

        CHECK(compiled_manifest.manifestStep() == -1);
      }
    }

    SECTION("dependency_cycle") {
      SECTION("Empty") {
        RawManifest raw_manifest;

        CompiledManifest manifest(manifest_path, std::move(raw_manifest));
        CHECK(manifest.dependencyCycle().empty());
      }

      SECTION("Single input") {
        RawManifest raw_manifest;
        raw_manifest.steps = { single_input };

        CompiledManifest manifest(manifest_path, std::move(raw_manifest));

        CHECK(manifest.dependencyCycle().empty());
      }

      SECTION("Single output") {
        RawManifest raw_manifest;
        raw_manifest.steps = { single_input };

        CompiledManifest manifest(manifest_path, std::move(raw_manifest));

        CHECK(manifest.dependencyCycle().empty());
      }

      SECTION("Single cyclic step through input") {
        RawStep cyclic_step;
        cyclic_step.outputs = { paths.get("a") };
        cyclic_step.inputs = { paths.get("a") };

        RawManifest raw_manifest;
        raw_manifest.steps = { cyclic_step };

        CompiledManifest manifest(manifest_path, std::move(raw_manifest));

        CHECK(manifest.dependencyCycle() == "a -> a");
      }

      SECTION("Single cyclic step through implicit input") {
        RawStep cyclic_step;
        cyclic_step.outputs = { paths.get("a") };
        cyclic_step.implicit_inputs = { paths.get("a") };

        RawManifest raw_manifest;
        raw_manifest.steps = { cyclic_step };

        CompiledManifest manifest(manifest_path, std::move(raw_manifest));

        CHECK(manifest.dependencyCycle() == "a -> a");
      }

      SECTION("Single cyclic step through dependency") {
        RawStep cyclic_step;
        cyclic_step.outputs = { paths.get("a") };
        cyclic_step.dependencies = { paths.get("a") };

        RawManifest raw_manifest;
        raw_manifest.steps = { cyclic_step };

        CompiledManifest manifest(manifest_path, std::move(raw_manifest));

        CHECK(manifest.dependencyCycle() == "a -> a");
      }

      SECTION("Two cyclic steps") {
        RawStep cyclic_step_1;
        cyclic_step_1.outputs = { paths.get("b") };
        cyclic_step_1.inputs = { paths.get("a") };
        RawStep cyclic_step_2;
        cyclic_step_2.outputs = { paths.get("a") };
        cyclic_step_2.inputs = { paths.get("b") };

        RawManifest raw_manifest;
        raw_manifest.steps = { cyclic_step_1, cyclic_step_2 };

        CompiledManifest manifest(manifest_path, std::move(raw_manifest));

        CHECK(manifest.dependencyCycle() == "a -> b -> a");
      }
    }
  }
}

}  // namespace detail
}  // namespace shk
