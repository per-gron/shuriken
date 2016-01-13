#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "path.h"

namespace shk {

void showValue(const Path &path, std::ostream &os);

namespace gen {

rc::Gen<std::vector<std::string>> pathComponents();

rc::Gen<std::string> pathString();

rc::Gen<shk::Path> path(const std::shared_ptr<Paths> &paths);

rc::Gen<std::vector<Path>> pathVector(const std::shared_ptr<Paths> &paths);

}  // namespace gen
}  // namespace shk
