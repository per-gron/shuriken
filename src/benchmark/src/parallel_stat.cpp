#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "benchmark_timer.h"

static const std::string kTmpFile = "bench_tmp.txt";
static constexpr int kNumFiles = 200000;


int main() {
  auto cmd =
      "find /System 2> /dev/null | head -n " +
      std::to_string(kNumFiles) +
      " > bench_tmp.txt";

  if (system(cmd.c_str()) != 0) {
    fprintf(stderr, "Failed to gather long list of files\n");
    return 1;
  }

  std::vector<std::string> files;
  std::ifstream file("bench_tmp.txt");
  std::string str;
  while (std::getline(file, str)) {
    files.push_back(str);
  }

  if (files.size() != kNumFiles) {
    fprintf(stderr, "Found %lu files, not %d\n", files.size(), kNumFiles);
    return 1;
  }

  printf("lstat-ing %d files\n", kNumFiles);
  for (int num_threads = 1; num_threads < 16; num_threads++) {
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    shk::BenchmarkTimer timer;

    for (int thread = 0; thread < num_threads; thread++) {
      threads.push_back(std::thread([&] {
        struct stat stat_buf;
        for (int file_idx = thread;
            file_idx < files.size();
            file_idx += num_threads) {
          lstat(files[file_idx].c_str(), &stat_buf);
        }
      }));
    }

    for (auto &thread : threads) {
      thread.join();
    }

    printf("With %d threads: ", num_threads);
    timer.printElapsedTime();
  }

  if (unlink("bench_tmp.txt") != 0) {
    fprintf(stderr, "Failed to unlink temporary file\n");
    return 1;
  }
  return 0;
}
