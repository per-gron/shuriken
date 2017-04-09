#pragma once

#include <chrono>

namespace shk {

class BenchmarkTimer {
 public:
  BenchmarkTimer()
      : _start(std::chrono::steady_clock::now()) {}

  std::chrono::duration<double> getElapsedTime() {
    auto end = std::chrono::steady_clock::now();
    return end - _start;
  }

  void printElapsedTime() {
    printf("Elapsed time: %fs\n", getElapsedTime().count());
  }

 private:
  std::chrono::time_point<std::chrono::steady_clock> _start;
};

}  // namespace shk
