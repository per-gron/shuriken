#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "path.h"

#include "generators.h"

namespace shk {

TEST_CASE("Path::operator==") {
  rc::prop("equal string paths are equal paths", []() {
    Paths paths;

    const auto path_1_string = *gen::pathString();
    const auto path_2_string = *gen::pathString();

    const auto path_1 = paths.get(path_1_string);
    const auto path_2 = paths.get(path_2_string);

    RC_ASSERT((path_1 == path_2) == (path_1_string == path_2_string));
  });
}

TEST_CASE("Path::basenameSplit") {
  rc::prop("extracts the basename and the dirname", []() {
    Paths paths;

    const auto path_components = *gen::pathComponents();
    RC_PRE(!path_components.empty());

    const auto path_string = gen::joinPathComponents(path_components);
    const auto dirname_string = gen::joinPathComponents(
        std::vector<std::string>(
            path_components.begin(),
            path_components.end() - 1));

    const auto path = paths.get(path_string);

    std::string dirname;
    std::string basename;
    std::tie(dirname, basename) = path.basenameSplit();

    RC_ASSERT(basename == *path_components.rbegin());
    RC_ASSERT(dirname == dirname_string);
  });
}

}  // namespace shk
