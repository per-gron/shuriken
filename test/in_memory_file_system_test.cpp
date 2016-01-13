#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "in_memory_file_system.h"
#include "generators.h"

namespace shk {

TEST_CASE("detail::basenameSplit") {
  rc::prop("extracts the basename and the dirname", []() {
    const auto path_components = *gen::pathComponents();
    RC_PRE(!path_components.empty());

    const auto path_string = gen::joinPathComponents(path_components);
    const auto dirname_string = gen::joinPathComponents(
        std::vector<std::string>(
            path_components.begin(),
            path_components.end() - 1));

    std::string dirname;
    std::string basename;
    std::tie(dirname, basename) = detail::basenameSplit(path_string);

    RC_ASSERT(basename == *path_components.rbegin());
    RC_ASSERT(dirname == dirname_string);
  });
}

}  // namespace shk
