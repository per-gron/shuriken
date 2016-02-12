#include <catch.hpp>
#include <rapidcheck/catch.h>

#include "path.h"

namespace shk {

void showValue(const Path &path, std::ostream &os);

namespace gen {

rc::Gen<std::string> pathComponent();

rc::Gen<std::vector<std::string>> pathComponents();

std::string joinPathComponents(const std::vector<std::string> &path_components);

rc::Gen<std::string> pathString();

rc::Gen<shk::Path> path(const std::shared_ptr<Paths> &paths);

rc::Gen<shk::Path> pathWithSingleComponent(const std::shared_ptr<Paths> &paths);

rc::Gen<std::unordered_set<std::string>> pathStringSet();

rc::Gen<std::vector<Path>> pathVector(const std::shared_ptr<Paths> &paths);

rc::Gen<std::vector<std::string>> pathStringWithSingleComponentVector();

}  // namespace gen
}  // namespace shk
