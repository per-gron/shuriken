cmake_minimum_required(VERSION 3.2.1)
project(shk)

add_subdirectory(vendor)

add_library(shklib
  src/build.h
  src/build.cpp
  src/build_status.h
  src/command_runner.h
  src/file_system.h
  src/fingerprint.cpp
  src/fingerprint.h
  src/hash.h
  src/invocation_log.h
  src/invocations.h
  src/io_error.h
  src/manifest_parser.h
  src/path.cpp
  src/path.h
  src/path_error.h
  src/persistent_file_system.cpp
  src/persistent_file_system.h
  src/step.h
  src/task.h)
set_property(TARGET shklib PROPERTY CXX_STANDARD 11)
target_include_directories(shklib PUBLIC src)

add_executable(shk
  src/shk.cpp)
set_property(TARGET shk PROPERTY CXX_STANDARD 11)
target_link_libraries(shk shklib)

add_executable(shk_test
  test/dummy_command_runner.cpp
  test/dummy_command_runner.h
  test/dummy_command_runner_test.cpp
  test/generators.cpp
  test/generators.h
  test/in_memory_file_system.cpp
  test/in_memory_file_system.h
  test/in_memory_file_system_test.cpp
  test/in_memory_invocation_log.cpp
  test/in_memory_invocation_log.h
  test/main.cpp
  test/path_test.cpp
  test/properties.cpp)
set_property(TARGET shk_test PROPERTY CXX_STANDARD 11)
target_include_directories(shklib PUBLIC vendor/rapidcheck/extras/catch/include)
target_link_libraries(shk_test shklib catch rapidcheck rapidcheck_catch)
