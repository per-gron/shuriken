#include "generators.h"

namespace shk {

void showValue(const Path &path, std::ostream &os) {
  os << "'" << path.canonicalized() << "'";
}

namespace gen {

rc::Gen<shk::Path> path(const std::shared_ptr<Paths> &paths) {
  return rc::gen::exec([paths] {
    const auto path_component_gen =
        rc::gen::container<std::string>(rc::gen::inRange<char>('a', 'z'));
    const auto path_components =
        *rc::gen::resize(
            10,
            rc::gen::container<std::vector<std::string>>(path_component_gen));
    std::string path;
    for (const auto &path_component : path_components) {
      if (!path.empty()) {
        path += "/";
      }
      path += path_component;
    }
    return paths->get(path);
  });
}

rc::Gen<std::vector<Path>> pathVector(const std::shared_ptr<Paths> &paths) {
  return rc::gen::container<std::vector<Path>>(path(paths));
}

}  // namespace gen
}  // namespace shk
