#include <catch.hpp>

#include "cmd/dry_run_command_runner.h"

namespace shk {

TEST_CASE("DryRunCommandRunner") {
  const auto runner = makeDryRunCommandRunner();

  CHECK(runner->canRunMore());
  CHECK(runner->size() == 0);
  CHECK(!runner->runCommands());

  bool invoked = false;
  runner->invoke("cmd", "a_pool", [&invoked](
      CommandRunner::Result &&result) {
    invoked = true;
  });
  CHECK(!invoked);
  CHECK(!runner->runCommands());
  CHECK(invoked);
}

}  // namespace shk
