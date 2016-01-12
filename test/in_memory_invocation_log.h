#include "invocation_log.h"
#include "invocations.h"

namespace shk {

/**
 * An InvocationLog implementation that is memory backed rather than disk based
 * like the real InvocationLog. Used for testing.
 */
class InMemoryInvocationLog : public InvocationLog {
 public:
  void createdDirectory(const Path &path) throw(IoError) override;
  void removedDirectory(const Path &path) throw(IoError) override;
  void ranCommand(
      const Hash &build_step_hash,
      const Invocations::Entry &entry) throw(IoError) override;
  void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) override;

  /**
   * Get the current Invocations
   */
  const Invocations &invocations() {
    return _invocations;
  }

 private:
  Invocations _invocations;
};

}  // namespace shk
