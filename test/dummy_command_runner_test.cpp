#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "dummy_command_runner.h"
#include "generators.h"

namespace shk {

TEST_CASE("DummyCommandRunner") {
  rc::prop("splitCommand of constructCommand should be an identity transformation", []() {
    const auto paths = std::make_shared<Paths>();
    const auto in_inputs = *gen::pathVector(paths);
    const auto in_outputs = *gen::pathVector(paths);

    const auto command = DummyCommandRunner::constructCommand(in_inputs, in_outputs);

    std::vector<Path> out_inputs;
    std::vector<Path> out_outputs;
    std::tie(out_inputs, out_outputs) = detail::splitCommand(*paths, command);

    RC_ASSERT(out_inputs == in_inputs);
    RC_ASSERT(out_outputs == out_outputs);
  });
}

}  // namespace shk
