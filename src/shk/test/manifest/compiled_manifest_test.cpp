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

}  // anonymous namespace

TEST_CASE("CompiledManifest") {
  InMemoryFileSystem fs;
  Paths paths(fs);
  RawManifest manifest;

  const auto manifest_path = paths.get("b.ninja");

  const auto get_manifest_compile_error = [&](const RawManifest &raw_manifest) {
    flatbuffers::FlatBufferBuilder builder;
    std::string err;
    bool success = CompiledManifest::compile(builder, manifest_path, raw_manifest, &err);
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

  SECTION("compile") {
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
        CHECK(err.empty());
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
        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(!compiled_manifest.steps()[0].generator());
        CHECK(compiled_manifest.steps()[0].generatorInputs().empty());
      }

      SECTION("true") {
        RawStep step;
        step.inputs = { paths.get("a") };
        step.implicit_inputs = { paths.get("a2") };
        step.dependencies = { paths.get("a3") };
        step.generator = true;

        RawManifest manifest;
        manifest.steps = { step };

        auto compiled_manifest = compile_manifest(manifest);
        REQUIRE(compiled_manifest.steps().size() == 1);
        CHECK(compiled_manifest.steps()[0].generator());
        CHECK(
            toVector(compiled_manifest.steps()[0].generatorInputs()) ==
            std::vector<nt_string_view>({ "a", "a2", "a3" }));
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
    CHECK(err.empty());

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

    SECTION("manifest_path") {
      setAtOffset(fb, ShkManifest::Manifest::VT_MANIFEST_STEP, 4);
      CHECK(get_load_error() == "Encountered invalid step index");
    }
  }
}

}  // namespace detail
}  // namespace shk
