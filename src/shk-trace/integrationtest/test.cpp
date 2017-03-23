#include <cstdio>
#include <string>
#include <unordered_map>

#include <util/file_descriptor.h>

namespace {

int fork() {
  printf("fork\n");

  // Fork:
  // * inherit fd

  return 0;
}

const std::unordered_map<std::string, std::function<int ()>> kTests = {
  { "fork", fork }
};

}  // anonymous namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s [test-name]\nAvailable tests:\n", argv[0]);
    for (const auto &test : kTests) {
      printf("  %s\n", test.first.c_str());
    }
    return 1;
  }

  const std::string test_name = argv[1];
  auto test_it = kTests.find(test_name);
  if (test_it == kTests.end()) {
    fprintf(stderr, "No test with name %s found.\n", test_name.c_str());
    return 1;
  }

  return test_it->second();
}
