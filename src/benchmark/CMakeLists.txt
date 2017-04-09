add_executable(parallel-stat
  src/benchmark_timer.h
  src/parallel_stat.cpp)
set_property(TARGET parallel-stat PROPERTY CXX_STANDARD 11)
set_target_properties(parallel-stat PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
