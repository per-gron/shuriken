#include "recompact.h"

#include "persistent_invocation_log.h"
#include "util.h"

namespace shk {

int toolRecompact(int argc, char *argv[], const ToolParams &params) {
  try {
    recompactPersistentInvocationLog(
        params.file_system,
        params.clock,
        params.invocations,
        params.invocation_log_path);
  } catch (const IoError &err) {
    error("failed recompaction: %s", err.what());
    return 1;
  }

  return 0;
}

}  // namespace shk
