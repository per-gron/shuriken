#include <catch.hpp>

#include "cmd/dry_run_command_runner.h"
#include "../manifest/step_builder.h"

namespace shk {

TEST_CASE("DryRunCommandRunner") {
  const auto runner = makeDryRunCommandRunner();

  CHECK(runner->canRunMore());
  CHECK(runner->size() == 0);
  CHECK(!runner->runCommands());

  bool invoked = false;
  flatbuffers::FlatBufferBuilder builder;
  runner->invoke("cmd", StepBuilder().build(builder), [&invoked](
      CommandRunner::Result &&result) {
    invoked = true;
  });
  CHECK(!invoked);
  CHECK(!runner->runCommands());
  CHECK(invoked);
}

}  // namespace shk
