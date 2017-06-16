add_executable(parallel-stat
  src/benchmark_timer.h
  src/parallel_stat.cpp)
set_property(TARGET parallel-stat PROPERTY CXX_STANDARD 14)
set_property(TARGET parallel-stat APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})