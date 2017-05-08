// Copyright 2017 Per Grön. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

template <typename TableType>
void setAtOffset(const TableType *table, int offset, int value) {
  const_cast<int *>(reinterpret_cast<const int *>(table))[
      reinterpret_cast<const flatbuffers::Table *>(table)
          ->GetOptionalFieldOffset(offset)] =
                  flatbuffers::EndianScalar(value);
}

std::string readFile(FileSystem &fs, nt_string_view path) {
  std::string data;
  IoError error;
  std::tie(data, error) = fs.readFile(path);
  CHECK(!error);
  return data;
}

}  // anonymous namespace

TEST_CASE("CompiledManifest") {
  time_t now = 0;
  InMemoryFileSystem fs([&] { return now; });
  Paths paths(fs);
  RawManifest manifest;

  const auto manifest_path = paths.get("b.ninja");

  const auto get_manifest_compile_error = [&](const RawManifest &raw_manifest) {
    flatbuffers::FlatBufferBuilder builder;
    std::string err;
    bool success = CompiledManifest::compile(
        builder, manifest_path, raw_manifest, &err);
    if (success) {
      CHECK(err == "");
    } else {
      CHECK(err != "");
    }
    return err;
  };

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

  using PathToStepList = std::vector<std::pair<nt_string_view, StepIndex>>;

  std::vector<std::unique_ptr<flatbuffers::FlatBufferBuilder>> builders;
  const auto compile_manifest = [&](
      const RawManifest &raw_manifest,
      bool allow_compile_error = false) {
    builders.emplace_back(new flatbuffers::FlatBufferBuilder(1024));
    auto &builder = *builders.back();
    std::string err;
    bool success = CompiledManifest::compile(
        builder, manifest_path, raw_manifest, &err);
    if (!allow_compile_error) {
      CHECK(success);
      CHECK(err == "");
    }
    err.clear();
    const auto maybe_manifest = CompiledManifest::load(
        string_view(
            reinterpret_cast<const char *>(builder.GetBufferPointer()),
            builder.GetSize()),
        &err);
    CHECK(err == "");
    CHECK(maybe_manifest);
    return *maybe_manifest;
  };

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

    SECTION("duplicate outputs from different steps") {
      CHECK_THROWS_AS(
          computeOutputPathMap({ single_output, single_output }), BuildError);
    }

    SECTION("duplicate outputs from a single step") {
      RawStep duplicate_outputs;
      duplicate_outputs.outputs = { paths.get("c"), paths.get("c") };

      auto map = computeOutputPathMap({ duplicate_outputs });
      CHECK(map.size() == 1);
      CHECK(map[paths.get("c")] == 0);
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

  SECTION("compile") {
    SECTION("basics") {
      RawManifest manifest;
      manifest.steps = { single_output };

      auto compiled_manifest = compile_manifest(manifest);

      REQUIRE(compiled_manifest.steps().size() == 1);
      CHECK(compiled_manifest.steps()[0].hash() == single_output.hash());
    }

    SECTION("disallow depfile+generator step") {
      RawStep step;
      step.generator = true;
      step.depfile = "hej";

      RawManifest manifest;
      manifest.steps = { step };

      auto error = get_manifest_compile_error(manifest);
      CHECK(error == "Generator build steps must not have depfile");
    }

    SECTION("inputs") {
      SECTION("empty") {
        RawManifest manifest;
        manifest.steps = { single_output };

        auto compiled_manifest = compile_manifest(manifest);

        CHECK(compiled_manifest.inputs().empty());
      }

      SECTION("inputs") {
        RawManifest manifest;
        manifest.steps = { single_input };

        auto compiled_manifest = compile_manifest(manifest);

        CHECK(
            toVector(compiled_manifest.inputs()) ==
            PathToStepList({ { "a", 0 } }));
      }

      SECTION("implicit_inputs") {
        RawManifest manifest;
        manifest.steps = { single_implicit_input };

        auto compiled_manifest = compile_manifest(manifest);

        CHECK(
            toVector(compiled_manifest.inputs()) ==
            PathToStepList({ { "a", 0 } }));
      }

      SECTION("dependencies") {
        RawManifest manifest;
        manifest.steps = { single_dependency };

        auto compiled_manifest = compile_manifest(manifest);

        CHECK(
            toVector(compiled_manifest.inputs()) ==
            PathToStepList({ { "a", 0 } }));
      }

      SECTION("shared inputs") {
        RawManifest manifest;
        manifest.steps = { single_dependency, single_input };

        auto compiled_manifest = compile_manifest(manifest);

        CHECK(
            toVector(compiled_manifest.inputs()) ==
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

          auto compiled_manifest = compile_manifest(manifest);

          CHECK(
              toVector(compiled_manifest.inputs()) ==
              PathToStepList({ { "a", swap_steps }, { "b", !swap_steps } }));
        }
      }

      SECTION("canonicalize path") {
        RawStep step;
        step.inputs = { paths.get("a/../b") };

        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);

        CHECK(
            toVector(compiled_manifest.inputs()) ==
            PathToStepList({ { "b", 0 } }));
      }
    }

    SECTION("outputs") {
      SECTION("empty") {
        RawManifest manifest;
        manifest.steps = { single_input };

        auto compiled_manifest = compile_manifest(manifest);

        CHECK(compiled_manifest.outputs().empty());
      }

      SECTION("outputs") {
        RawManifest manifest;
        manifest.steps = { single_output };

        auto compiled_manifest = compile_manifest(manifest);

        CHECK(
            toVector(compiled_manifest.outputs()) ==
            PathToStepList({ { "a", 0 } }));
      }

      SECTION("duplicate outputs in the same step") {
        RawStep duplicate_outputs;
        duplicate_outputs.outputs = { paths.get("a"), paths.get("a") };

        RawManifest manifest;
        manifest.steps = { duplicate_outputs };

        auto compiled_manifest = compile_manifest(manifest);

        CHECK(
            toVector(compiled_manifest.outputs()) ==
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

          auto compiled_manifest = compile_manifest(manifest);

          CHECK(
              toVector(compiled_manifest.outputs()) ==
              PathToStepList({ { "a", swap_steps }, { "b", !swap_steps } }));
        }
      }

      SECTION("canonicalize path") {
        RawStep step;
        step.outputs = { paths.get("a/../b") };

        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);

        CHECK(
            toVector(compiled_manifest.outputs()) ==
            PathToStepList({ { "b", 0 } }));
      }
    }

    SECTION("input without corresponding step") {
      RawManifest manifest;
      manifest.steps = { single_input };

      auto compiled_manifest = compile_manifest(manifest);
      REQUIRE(compiled_manifest.steps().size() == 1);
      CHECK(
          toVector(compiled_manifest.steps()[0].dependencies()) ==
          std::vector<StepIndex>{});
    }

    SECTION("inputs") {
      RawManifest manifest;
      manifest.steps = { single_output, single_input };

      auto compiled_manifest = compile_manifest(manifest);
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

      auto compiled_manifest = compile_manifest(manifest);
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

      auto compiled_manifest = compile_manifest(manifest);
      REQUIRE(compiled_manifest.steps().size() == 2);
      CHECK(
          toVector(compiled_manifest.steps()[0].dependencies()) ==
          std::vector<StepIndex>{});
      CHECK(
          toVector(compiled_manifest.steps()[1].dependencies()) ==
          std::vector<StepIndex>{ 0 });
    }

    SECTION("non-generator with generator build steps") {
      SECTION("dependency from non-generator to generator step") {
        single_output.command = "not_phony";
        single_output.generator = true;

        const RawStep *steps[] = {
            &single_input, &single_implicit_input, &single_dependency };
        for (const auto *step : steps) {
          auto step_copy = *step;
          step_copy.command = "not_phony";

          RawManifest manifest;
          manifest.steps = { single_output, step_copy };

          auto error = get_manifest_compile_error(manifest);
          CHECK(
              error ==
              "Normal build steps must not depend on generator build steps");
        }
      }

      SECTION("dependency from generator to non-generator step") {
        single_output.command = "not_phony";

        const RawStep *steps[] = {
            &single_input, &single_implicit_input, &single_dependency };
        for (const auto *step : steps) {
          auto step_copy = *step;
          step_copy.command = "not_phony";
          step_copy.generator = true;

          RawManifest manifest;
          manifest.steps = { single_output, step_copy };

          auto error = get_manifest_compile_error(manifest);
          CHECK(
              error ==
              "Generator build steps must not depend on normal build steps");
        }
      }

      SECTION("dependency from generator to phony step") {
        single_input.command = "not_phony";
        single_input.generator = true;

        single_output.command = "";  // phony step

        RawManifest manifest;
        manifest.steps = { single_output, single_input };

        CHECK(get_manifest_compile_error(manifest) == "");
      }

      SECTION("dependency from phony to generator step") {
        single_output.command = "not_phony";
        single_output.generator = true;

        single_input.command = "";  // phony step

        RawManifest manifest;
        manifest.steps = { single_output, single_input };

        CHECK(get_manifest_compile_error(manifest) == "");
      }

      SECTION("dependency from generator to non-generator step via phony") {
        RawStep generator;
        generator.generator = true;
        generator.command = "not_phony";
        generator.inputs = { paths.get("b") };

        RawStep phony;
        phony.outputs = { paths.get("b") };
        phony.inputs = { paths.get("a") };

        RawStep non_generator;
        non_generator.command = "non_phony";
        non_generator.outputs = { paths.get("a") };

        RawManifest manifest;
        manifest.steps = { generator, phony, non_generator };

        CHECK(
            get_manifest_compile_error(manifest) ==
            "Generator build steps must not depend on normal build steps");
      }

      SECTION("dependency from non-generator to generator step via phony") {
        RawStep non_generator;
        non_generator.command = "not_phony";
        non_generator.inputs = { paths.get("b") };

        RawStep phony;
        phony.outputs = { paths.get("b") };
        phony.inputs = { paths.get("a") };

        RawStep generator;
        generator.generator = true;
        generator.command = "non_phony";
        generator.outputs = { paths.get("a") };

        RawManifest manifest;
        manifest.steps = { generator, phony, non_generator };

        CHECK(
            get_manifest_compile_error(manifest) ==
            "Normal build steps must not depend on generator build steps");
      }
    }

    SECTION("sort dependencies") {
      RawStep two_dependencies;
      two_dependencies.inputs = { paths.get("a"), paths.get("b") };
      RawStep two_dependencies_reversed;
      two_dependencies_reversed.inputs = { paths.get("b"), paths.get("a") };

      RawManifest manifest;
      manifest.steps = {
          single_output,
          single_output_b,
          two_dependencies,
          two_dependencies_reversed };

      auto compiled_manifest = compile_manifest(manifest);
      REQUIRE(compiled_manifest.steps().size() == 4);
      CHECK(
          toVector(compiled_manifest.steps()[0].dependencies()) ==
          std::vector<StepIndex>{});
      CHECK(
          toVector(compiled_manifest.steps()[1].dependencies()) ==
          std::vector<StepIndex>{});
      CHECK(
          toVector(compiled_manifest.steps()[2].dependencies()) ==
          std::vector<StepIndex>({ 0, 1 }));
      CHECK(
          toVector(compiled_manifest.steps()[3].dependencies()) ==
          std::vector<StepIndex>({ 0, 1 }));
    }

    SECTION("deduplicate dependencies") {
      RawStep duplicate_dependencies;
      duplicate_dependencies.inputs =
          { paths.get("a"), paths.get("a") };
      duplicate_dependencies.implicit_inputs =
          { paths.get("a"), paths.get("a") };
      duplicate_dependencies.dependencies =
          { paths.get("a"), paths.get("a") };

      RawManifest manifest;
      manifest.steps = { single_output, duplicate_dependencies };

      auto compiled_manifest = compile_manifest(manifest);
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

        auto compiled_manifest = compile_manifest(manifest);
        CHECK(compiled_manifest.defaults().empty());
      }

      SECTION("one") {
        RawManifest manifest;
        manifest.steps = { single_output, single_output_b };
        manifest.defaults = { paths.get("b") };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.defaults().size() == 1);
        CHECK(compiled_manifest.defaults()[0] == 1);
      }

      SECTION("two") {
        RawManifest manifest;
        manifest.steps = { single_output, single_output_b };
        manifest.defaults = { paths.get("b"), paths.get("a") };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.defaults().size() == 2);
        CHECK(compiled_manifest.defaults()[0] == 1);
        CHECK(compiled_manifest.defaults()[1] == 0);
      }
    }

    SECTION("roots") {
      SECTION("empty") {
        RawManifest manifest;
        auto compiled_manifest = compile_manifest(manifest);
        CHECK(compiled_manifest.roots().empty());
      }

      SECTION("single empty step") {
        RawManifest manifest;
        manifest.steps = { empty };

        auto compiled_manifest = compile_manifest(manifest);
        CHECK(
            toVector(compiled_manifest.roots()) ==
            std::vector<StepIndex>{ 0 });
      }

      SECTION("two empty steps") {
        RawManifest manifest;
        manifest.steps = { empty, empty };

        auto compiled_manifest = compile_manifest(manifest);
        CHECK(
            toVector(compiled_manifest.roots()) ==
            std::vector<StepIndex>({ 0, 1 }));
      }

      SECTION("one step depending on another") {
        RawManifest manifest;
        manifest.steps = { single_output, single_input };

        auto compiled_manifest = compile_manifest(manifest);
        CHECK(
            toVector(compiled_manifest.roots()) ==
            std::vector<StepIndex>{ 1 });
      }

      SECTION("one step depending on another, reverse order") {
        RawManifest manifest;
        manifest.steps = { single_input, single_output };

        auto compiled_manifest = compile_manifest(manifest);
        CHECK(
            toVector(compiled_manifest.roots()) ==
            std::vector<StepIndex>{ 0 });
      }

      SECTION("one step depending on another plus independent step") {
        RawManifest manifest;
        manifest.steps = { single_output, single_input, empty };

        auto compiled_manifest = compile_manifest(manifest);
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

        auto compiled_manifest = compile_manifest(
            manifest, /*allow_compile_error:*/true);
        CHECK(
            toVector(compiled_manifest.roots()) ==
            std::vector<StepIndex>{});
      }
    }

    SECTION("pools") {
      SECTION("empty") {
        RawManifest manifest;

        auto compiled_manifest = compile_manifest(manifest);
        CHECK(
            toVector(compiled_manifest.pools()) ==
            (std::vector<std::pair<nt_string_view, int>>{}));
      }

      SECTION("one") {
        RawManifest manifest;
        manifest.pools["a_pool"] = 123;

        auto compiled_manifest = compile_manifest(manifest);
        CHECK(
            toVector(compiled_manifest.pools()) ==
            (std::vector<std::pair<nt_string_view, int>>(
                { { "a_pool", 123 } })));
      }
    }

    SECTION("build_dir") {
      RawManifest manifest;
      manifest.build_dir = "hello";

      auto compiled_manifest = compile_manifest(manifest);
      CHECK(compiled_manifest.buildDir() == "hello");
    }

    SECTION("output dirs") {
      SECTION("current working directory") {
        RawStep step;
        step.outputs = { paths.get("a") };

        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].outputDirs().size() == 0);
      }

      SECTION("one directory") {
        RawStep step;
        step.outputs = { paths.get("dir/a") };

        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);
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

        auto compiled_manifest = compile_manifest(manifest);
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

        auto compiled_manifest = compile_manifest(manifest);
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

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].outputDirs().size() == 1);
        CHECK(contains(compiled_manifest.steps()[0].outputDirs(), "dir"));
      }
    }

    SECTION("pool name") {
      SECTION("no value") {
        RawManifest manifest;
        manifest.steps = { RawStep() };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].poolName() == "");
      }

      SECTION("with value") {
        RawStep step;
        step.pool_name = "my_pool";

        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].poolName() == "my_pool");
      }
    }

    SECTION("command") {
      SECTION("no value") {
        RawManifest manifest;
        manifest.steps = { RawStep() };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].command() == "");
      }

      SECTION("with value") {
        RawStep step;
        step.command = "my_cmd";

        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].command() == "my_cmd");
      }
    }

    SECTION("description") {
      SECTION("no value") {
        RawManifest manifest;
        manifest.steps = { RawStep() };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].description() == "");
      }

      SECTION("with value") {
        RawStep step;
        step.description = "my_description";

        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].description() == "my_description");
      }
    }

    SECTION("depfile") {
      SECTION("no value") {
        RawManifest manifest;
        manifest.steps = { RawStep() };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].depfile() == "");
      }

      SECTION("with value") {
        RawStep step;
        step.depfile = "my_depfile";

        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].depfile() == "my_depfile");
      }
    }

    SECTION("rspfile") {
      SECTION("no value") {
        RawManifest manifest;
        manifest.steps = { RawStep() };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].rspfile() == "");
      }

      SECTION("with value") {
        RawStep step;
        step.rspfile = "my_rspfile";

        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].rspfile() == "my_rspfile");
      }
    }

    SECTION("rspfileContent") {
      SECTION("no value") {
        RawManifest manifest;
        manifest.steps = { RawStep() };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].rspfileContent() == "");
      }

      SECTION("with value") {
        RawStep step;
        step.rspfile_content = "my_rspfile_c";

        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].rspfileContent() == "my_rspfile_c");
      }
    }

    SECTION("generator") {
      SECTION("false") {
        RawStep step;
        step.inputs = { paths.get("a") };
        step.implicit_inputs = { paths.get("a2") };
        step.dependencies = { paths.get("a3") };
        step.outputs = { paths.get("b") };
        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(!compiled_manifest.steps()[0].generator());
        CHECK(compiled_manifest.steps()[0].generatorInputs().empty());
        CHECK(compiled_manifest.steps()[0].generatorOutputs().empty());
      }

      SECTION("true") {
        RawStep step;
        step.inputs = { paths.get("a") };
        step.implicit_inputs = { paths.get("a2") };
        step.dependencies = { paths.get("a3") };
        step.outputs = { paths.get("b") };
        step.generator = true;

        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].generator());
        CHECK(
            toVector(compiled_manifest.steps()[0].generatorInputs()) ==
            std::vector<nt_string_view>({ "a", "a2", "a3" }));
        CHECK(
            toVector(compiled_manifest.steps()[0].generatorOutputs()) ==
            std::vector<nt_string_view>({ "b" }));
      }
    }

    SECTION("manifest_step") {
      SECTION("present") {
        RawStep step;
        step.outputs = { manifest_path };

        RawManifest manifest;
        manifest.steps = { single_input, step };

        auto compiled_manifest = compile_manifest(manifest);

        CHECK(compiled_manifest.manifestStep() == 1);
      }

      SECTION("missing") {
        RawManifest manifest;
        manifest.steps = { single_input };

        auto compiled_manifest = compile_manifest(manifest);

        CHECK(!compiled_manifest.manifestStep());
      }
    }

    SECTION("manifest_files") {
      SECTION("present") {
        RawManifest manifest;
        manifest.manifest_files = { "one", "two" };

        auto compiled_manifest = compile_manifest(manifest);
        CHECK(
            toVector(compiled_manifest.manifestFiles()) ==
            std::vector<nt_string_view>({ "one", "two" }));
      }

      SECTION("missing") {
        RawManifest manifest;
        auto compiled_manifest = compile_manifest(manifest);
        CHECK(compiled_manifest.manifestFiles().empty());
      }
    }

    SECTION("dependency_cycle") {
      SECTION("Empty") {
        RawManifest raw_manifest;

        CHECK(get_manifest_compile_error(raw_manifest).empty());
      }

      SECTION("Single input") {
        RawManifest raw_manifest;
        raw_manifest.steps = { single_input };

        CHECK(get_manifest_compile_error(raw_manifest).empty());
      }

      SECTION("Single output") {
        RawManifest raw_manifest;
        raw_manifest.steps = { single_input };

        CHECK(get_manifest_compile_error(raw_manifest).empty());
      }

      SECTION("Single cyclic step through input") {
        RawStep cyclic_step;
        cyclic_step.outputs = { paths.get("a") };
        cyclic_step.inputs = { paths.get("a") };

        RawManifest raw_manifest;
        raw_manifest.steps = { cyclic_step };

        CHECK(
            get_manifest_compile_error(raw_manifest) ==
            "Dependency cycle: a -> a");
      }

      SECTION("Single cyclic step through implicit input") {
        RawStep cyclic_step;
        cyclic_step.outputs = { paths.get("a") };
        cyclic_step.implicit_inputs = { paths.get("a") };

        RawManifest raw_manifest;
        raw_manifest.steps = { cyclic_step };

        CHECK(
            get_manifest_compile_error(raw_manifest) ==
            "Dependency cycle: a -> a");
      }

      SECTION("Single cyclic step through dependency") {
        RawStep cyclic_step;
        cyclic_step.outputs = { paths.get("a") };
        cyclic_step.dependencies = { paths.get("a") };

        RawManifest raw_manifest;
        raw_manifest.steps = { cyclic_step };

        CHECK(
            get_manifest_compile_error(raw_manifest) ==
            "Dependency cycle: a -> a");
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

        CHECK(
            get_manifest_compile_error(raw_manifest) ==
            "Dependency cycle: a -> b -> a");
      }
    }
  }

  SECTION("load") {
    RawManifest manifest;
    manifest.steps = { empty, single_input, single_output };
    manifest.defaults = { paths.get("a") };
    manifest.pools["a_pool"] = 5;

    flatbuffers::FlatBufferBuilder builder;
    std::string err;
    CHECK(CompiledManifest::compile(
        builder, paths.get("a"), manifest, &err));
    CHECK(err == "");

    auto *fb = ShkManifest::GetManifest(builder.GetBufferPointer());

    const auto get_load_error = [&]() {
      const auto maybe_manifest = CompiledManifest::load(
          string_view(
              reinterpret_cast<const char *>(builder.GetBufferPointer()),
              builder.GetSize()),
          &err);
      CHECK(!maybe_manifest);
      return err;
    };

    SECTION("outputs") {
      REQUIRE(fb->outputs());
      REQUIRE(fb->outputs()->size() == 1);
      const_cast<char *>(reinterpret_cast<const char *>(
          fb->outputs()->Get(0)))[
              // +3 here is for modifying the least significant byte. Assumes
              // little endian.
              ShkManifest::StepPathReference::VT_STEP + 3] += 3;
      CHECK(get_load_error() == "Encountered invalid step index");
    }

    SECTION("inputs") {
      REQUIRE(fb->inputs());
      REQUIRE(fb->inputs()->size() == 1);
      const_cast<char *>(reinterpret_cast<const char *>(
          fb->inputs()->Get(0)))[
              // +3 here is for modifying the least significant byte. Assumes
              // little endian.
              ShkManifest::StepPathReference::VT_STEP + 3] += 3;
      CHECK(get_load_error() == "Encountered invalid step index");
    }

    SECTION("step.dependencies") {
      REQUIRE(fb->steps());
      REQUIRE(fb->steps()->size() == 3);
      auto &step = *fb->steps()->Get(1);
      REQUIRE(step.dependencies());
      REQUIRE(step.dependencies()->size() == 1);
      const_cast<flatbuffers::Vector<int32_t> *>(
          step.dependencies())->Mutate(0, 4);
      CHECK(get_load_error() == "Encountered invalid step index");
    }

    SECTION("defaults") {
      REQUIRE(fb->defaults());
      REQUIRE(fb->defaults()->size() == 1);
      const_cast<flatbuffers::Vector<int32_t> *>(
          fb->defaults())->Mutate(0, 4);
      CHECK(get_load_error() == "Encountered invalid step index");
    }

    SECTION("roots") {
      REQUIRE(fb->roots());
      REQUIRE(fb->roots()->size() >= 1);
      const_cast<flatbuffers::Vector<int32_t> *>(
          fb->roots())->Mutate(0, 4);
      CHECK(get_load_error() == "Encountered invalid step index");
    }

    SECTION("pools.depth") {
      REQUIRE(fb->pools());
      REQUIRE(fb->pools()->size() >= 1);
      setAtOffset(fb->pools()->Get(0), ShkManifest::Pool::VT_DEPTH, -1);
      CHECK(get_load_error() == "Encountered invalid step index");
    }

    SECTION("manifest_step") {
      flatbuffers::FlatBufferBuilder builder;

      std::vector<flatbuffers::Offset<ShkManifest::StepPathReference>> p_refs;
      auto outputs_vector = builder.CreateVector(p_refs.data(), p_refs.size());
      auto inputs_vector = builder.CreateVector(p_refs.data(), p_refs.size());

      std::vector<flatbuffers::Offset<ShkManifest::Step>> steps;
      auto steps_vector = builder.CreateVector(steps.data(), steps.size());

      std::vector<StepIndex> s_indices;
      auto defaults_vector = builder.CreateVector(
          s_indices.data(), s_indices.size());

      auto roots_vector = builder.CreateVector(
          s_indices.data(), s_indices.size());

      std::vector<flatbuffers::Offset<ShkManifest::Pool>> pools;
      auto pools_vector = builder.CreateVector(pools.data(), pools.size());

      std::vector<flatbuffers::Offset<flatbuffers::String>> manifest_files;
      auto manifest_files_vector = builder.CreateVector(
          manifest_files.data(), manifest_files.size());

      ShkManifest::ManifestBuilder manifest_builder(builder);
      manifest_builder.add_outputs(outputs_vector);
      manifest_builder.add_inputs(inputs_vector);
      manifest_builder.add_steps(steps_vector);
      manifest_builder.add_defaults(defaults_vector);
      manifest_builder.add_roots(roots_vector);
      manifest_builder.add_pools(pools_vector);
      manifest_builder.add_manifest_step(4);
      manifest_builder.add_manifest_files(manifest_files_vector);
      builder.Finish(manifest_builder.Finish());

      CHECK(!CompiledManifest::load(
          string_view(
              reinterpret_cast<const char *>(builder.GetBufferPointer()),
              builder.GetSize()),
          &err));
      CHECK(err == "Encountered invalid step index");
    }
  }

  SECTION("maxMtime, minMtime") {
    SECTION("empty") {
      RawManifest raw_manifest;
      auto manifest = compile_manifest(raw_manifest);

      CHECK(!CompiledManifest::maxMtime(fs, manifest.manifestFiles()));
      CHECK(!CompiledManifest::minMtime(fs, manifest.manifestFiles()));
    }

    SECTION("one missing file") {
      RawManifest raw_manifest;
      raw_manifest.manifest_files = { "missing" };
      auto manifest = compile_manifest(raw_manifest);

      CHECK(!CompiledManifest::maxMtime(fs, manifest.manifestFiles()));
      CHECK(!CompiledManifest::minMtime(fs, manifest.manifestFiles()));
    }

    SECTION("one file") {
      now = 1;
      CHECK(fs.writeFile("file", "") == IoError::success());

      RawManifest raw_manifest;
      raw_manifest.manifest_files = { "file" };
      auto manifest = compile_manifest(raw_manifest);

      CHECK(CompiledManifest::maxMtime(fs, manifest.manifestFiles()) == 1);
      CHECK(CompiledManifest::minMtime(fs, manifest.manifestFiles()) == 1);
    }

    SECTION("two files") {
      now = 1;
      CHECK(fs.writeFile("file1", "") == IoError::success());
      now = 2;
      CHECK(fs.writeFile("file2", "") == IoError::success());

      RawManifest raw_manifest;
      raw_manifest.manifest_files = { "file1", "file2" };
      auto manifest = compile_manifest(raw_manifest);

      CHECK(CompiledManifest::maxMtime(fs, manifest.manifestFiles()) == 2);
      CHECK(CompiledManifest::minMtime(fs, manifest.manifestFiles()) == 1);
    }

    SECTION("one missing one present") {
      now = 1;
      CHECK(fs.writeFile("file", "") == IoError::success());

      RawManifest raw_manifest;
      raw_manifest.manifest_files = { "file", "missing" };
      auto manifest = compile_manifest(raw_manifest);

      CHECK(!CompiledManifest::maxMtime(fs, manifest.manifestFiles()));
      CHECK(!CompiledManifest::minMtime(fs, manifest.manifestFiles()));
    }
  }

  SECTION("parseAndCompile") {
    std::vector<std::string> compiled_bufs;
    const auto parse_compiled_manifest = [&](const nt_string_view path) {
      const auto buffer = readFile(fs, path);

      REQUIRE(buffer.size() >= sizeof(uint64_t));
      const uint64_t version =
          flatbuffers::EndianScalar(
              *reinterpret_cast<const decltype(version) *>(
                  buffer.data()));
      CHECK(version == 1);

      compiled_bufs.emplace_back(
          readFile(fs, path));

      std::string err;
      const auto maybe_manifest = CompiledManifest::load(
          string_view(
              compiled_bufs.back().data() + sizeof(version),
              compiled_bufs.back().size() - sizeof(version)),
          &err);
      CHECK(err == "");
      REQUIRE(maybe_manifest);
      return *maybe_manifest;
    };

    const auto check_first_step_cmd = [](
        CompiledManifest manifest, nt_string_view cmd) {
      REQUIRE(manifest.steps().size() >= 1);
      CHECK(manifest.steps()[0].command() == cmd);
    };

    std::vector<std::shared_ptr<void>> memory_bufs;
    const auto parse_and_compile = [&](
        const std::string &path,
        const std::string &compiled_path) {
      std::string err;
      Optional<CompiledManifest> maybe_manifest;
      std::shared_ptr<void> buffer;
      std::tie(maybe_manifest, buffer) = CompiledManifest::parseAndCompile(
          fs, path, compiled_path, &err);
      CHECK(buffer);
      CHECK(err == "");
      REQUIRE(maybe_manifest);
      memory_bufs.push_back(buffer);
      return *maybe_manifest;
    };

    const auto parse_and_compile_fail = [&](
        const std::string &path,
        const std::string &compiled_path) {
      const bool compiled_manifest_existed_before =
          fs.stat(compiled_path).result == 0;

      std::string err;
      Optional<CompiledManifest> maybe_manifest;
      std::shared_ptr<void> buffer;
      std::tie(maybe_manifest, buffer) = CompiledManifest::parseAndCompile(
          fs, path, compiled_path, &err);

      CHECK(err != "");
      CHECK(!maybe_manifest);
      if (!compiled_manifest_existed_before) {
        CHECK(fs.stat(compiled_path).result == ENOENT);
      }
      return err;
    };

    SECTION("basic success") {
      const auto manifest_str =
          "rule cmd\n"
          "  command = cmd\n"
          "build out: cmd in\n";

      CHECK(fs.writeFile("manifest", manifest_str) == IoError::success());

      const auto manifest = parse_and_compile("manifest", "manifest.compiled");
      check_first_step_cmd(manifest, "cmd");
    }

    SECTION("parse error") {
      CHECK(fs.writeFile("manifest", "rule!\n") == IoError::success());

      CHECK(
          parse_and_compile_fail("manifest", "manifest.compiled") ==
          "failed to parse manifest: manifest:1: expected rule name\n");
    }

    SECTION("io error") {
      CHECK(fs.mkdir("manifest") == IoError::success());

      CHECK(
          parse_and_compile_fail("manifest", "manifest.compiled") ==
          "failed to parse manifest: loading 'manifest': "
          "The named file is a directory");
    }

    SECTION("compile error") {
      const auto manifest_str =
          "rule cmd\n"
          "  command = cmd\n"
          "build out: cmd in\n"
          "build in: cmd out\n";
      CHECK(fs.writeFile("manifest", manifest_str) == IoError::success());

      CHECK(
          parse_and_compile_fail("manifest", "manifest.compiled") ==
          "Dependency cycle: in -> out -> in");
    }

    SECTION("write compiled manifest") {
      const auto manifest_str =
          "rule cmd\n"
          "  command = cmd\n"
          "build out: cmd in\n";
      CHECK(fs.writeFile("manifest", manifest_str) == IoError::success());

      parse_and_compile("manifest", "manifest.compiled");

      const auto compiled_manifest =
          parse_compiled_manifest("manifest.compiled");
      check_first_step_cmd(compiled_manifest, "cmd");
    }

    SECTION("fail to write compiled manifest") {
      const auto manifest_str =
          "rule cmd\n"
          "  command = cmd\n"
          "build out: cmd in\n";

      CHECK(fs.writeFile("manifest", manifest_str) == IoError::success());
      CHECK(fs.mkdir("manifest.compiled") == IoError::success());

      CHECK(parse_and_compile_fail("manifest", "manifest.compiled") != "");
    }

    SECTION("fail to parse precompiled manifest") {
      const auto manifest_str =
          "rule cmd\n"
          "  command = cmd\n"
          "build out: cmd in\n";

      CHECK(fs.writeFile("manifest", manifest_str) == IoError::success());
      CHECK(
          fs.writeFile("manifest.compiled", "invalid_haha") ==
          IoError::success());

      // Should just recompile the manifest in this case
      const auto manifest = parse_and_compile("manifest", "manifest.compiled");
      check_first_step_cmd(manifest, "cmd");
      check_first_step_cmd(
          parse_compiled_manifest("manifest.compiled"),
          "cmd");
    }

    SECTION("empty precompiled manifest") {
      const auto manifest_str =
          "rule cmd\n"
          "  command = cmd\n"
          "build out: cmd in\n";

      CHECK(fs.writeFile("manifest", manifest_str) == IoError::success());
      CHECK(fs.writeFile("manifest.compiled", "") == IoError::success());

      // Should just recompile the manifest in this case
      const auto manifest = parse_and_compile("manifest", "manifest.compiled");
      check_first_step_cmd(manifest, "cmd");
      check_first_step_cmd(
          parse_compiled_manifest("manifest.compiled"),
          "cmd");
    }

    SECTION("precompiled manifest is older than one of its inputs") {
      const auto manifest_str =
          "rule cmd\n"
          "  command = cmd_$in\n"
          "include manifest.other\n";
      const auto other_manifest_str =
          "build out: cmd before\n";

      CHECK(fs.writeFile("manifest", manifest_str) == IoError::success());
      CHECK(
          fs.writeFile("manifest.other", other_manifest_str) ==
          IoError::success());
      now++;

      parse_and_compile("manifest", "manifest.compiled");
      check_first_step_cmd(
          parse_compiled_manifest("manifest.compiled"),
          "cmd_before");

      CHECK(
          fs.writeFile("manifest.other", "build out: cmd after\n") ==
          IoError::success());

      const auto manifest = parse_and_compile("manifest", "manifest.compiled");

      check_first_step_cmd(manifest, "cmd_after");
      check_first_step_cmd(
          parse_compiled_manifest("manifest.compiled"),
          "cmd_after");
    }

    SECTION("recompile with equal timestamps") {
      const auto manifest_str =
          "rule cmd\n"
          "  command = cmd_$in\n"
          "include manifest.other\n";
      const auto other_manifest_str =
          "build out: cmd before\n";

      CHECK(fs.writeFile("manifest", manifest_str) == IoError::success());
      CHECK(
          fs.writeFile("manifest.other", other_manifest_str) ==
          IoError::success());

      parse_and_compile("manifest", "manifest.compiled");
      check_first_step_cmd(
          parse_compiled_manifest("manifest.compiled"),
          "cmd_before");

      CHECK(
          fs.writeFile("manifest.other", "build out: cmd after\n") ==
          IoError::success());

      const auto manifest = parse_and_compile("manifest", "manifest.compiled");

      check_first_step_cmd(manifest, "cmd_after");
      check_first_step_cmd(
          parse_compiled_manifest("manifest.compiled"),
          "cmd_after");
    }

    SECTION("precompiled manifest input missing") {
      const auto manifest_str =
          "rule cmd\n"
          "  command = cmd_$in\n"
          "include manifest.other\n";
      const auto other_manifest_str =
          "build out: cmd before\n";

      CHECK(fs.writeFile("manifest", manifest_str) == IoError::success());
      CHECK(
          fs.writeFile("manifest.other", other_manifest_str) ==
          IoError::success());

      parse_and_compile("manifest", "manifest.compiled");
      check_first_step_cmd(
          parse_compiled_manifest("manifest.compiled"),
          "cmd_before");

      CHECK(fs.unlink("manifest.other") == IoError::success());

      parse_and_compile_fail("manifest", "manifest.compiled");
    }

    SECTION("precompiled manifest has wrong version") {
      const auto manifest_str =
          "rule cmd\n"
          "  command = cmd\n"
          "build out: cmd in\n";
      CHECK(fs.writeFile("manifest", manifest_str) == IoError::success());
      now++;

      parse_and_compile("manifest", "manifest.compiled");

      auto compiled_buf = readFile(fs, "manifest.compiled");
      REQUIRE(compiled_buf.size() >= sizeof(uint64_t));
      compiled_buf[0]++;
      CHECK(
          fs.writeFile("manifest.compiled", compiled_buf) ==
          IoError::success());

      now--;  // Go to the "past" to not trigger the mtime invalidation check
      const auto new_manifest_str =
          "rule cmd\n"
          "  command = other_cmd\n"
          "build out: cmd in\n";
      CHECK(fs.writeFile("manifest", new_manifest_str) == IoError::success());
      now++;

      const auto manifest = parse_and_compile("manifest", "manifest.compiled");

      check_first_step_cmd(manifest, "other_cmd");
      check_first_step_cmd(
          parse_compiled_manifest("manifest.compiled"),
          "other_cmd");
    }

    SECTION("parse precompiled manifest") {
      const auto manifest_str =
          "rule cmd\n"
          "  command = cmd\n"
          "build out: cmd in\n";
      CHECK(fs.writeFile("manifest", manifest_str) == IoError::success());

      parse_and_compile("manifest", "manifest.compiled");
      const auto manifest = parse_and_compile("manifest", "manifest.compiled");
      check_first_step_cmd(manifest, "cmd");
    }
  }
}

}  // namespace detail
}  // namespace shk
