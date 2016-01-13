#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "path.h"

#include "generators.h"

namespace shk {

TEST_CASE("Path") {
  rc::prop("equal paths count as same", []() {
    Paths paths;

    const auto path_1_string = *gen::pathString();
    const auto path_2_string = *gen::pathString();

    const auto path_1 = paths.get(path_1_string);
    const auto path_2 = paths.get(path_2_string);

    RC_ASSERT((path_1 == path_2) == (path_1_string == path_2_string));
  });
}

}  // namespace shk
