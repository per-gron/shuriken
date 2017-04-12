#include <catch.hpp>

#include "log/invocation_log.h"

#include "../in_memory_file_system.h"
#include "../in_memory_invocation_log.h"

namespace shk {

TEST_CASE("InvocationLog") {
  InMemoryFileSystem fs;
  fs.writeFile("a", "hello!");

  time_t now = 234;
  const auto clock = [&]{ return now; };
  InMemoryInvocationLog log(fs, clock);

  SECTION("FingerprintFiles") {
    SECTION("Empty") {
      CHECK(log.fingerprintFiles({}) == std::vector<Fingerprint>{});
    }

    SECTION("SingleFile") {
      CHECK(
          log.fingerprintFiles({ "a" }) ==
          std::vector<Fingerprint>({ takeFingerprint(fs, now, "a").first }));
    }

    SECTION("MultipleFiles") {
      CHECK(
          log.fingerprintFiles({ "a", "a", "missing" }) ==
          std::vector<Fingerprint>({
              takeFingerprint(fs, now, "a").first,
              takeFingerprint(fs, now, "a").first,
              takeFingerprint(fs, now, "missing").first }));
    }
  }
}

}  // namespace shk
