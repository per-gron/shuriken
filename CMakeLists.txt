cmake_minimum_required(VERSION 3.2.1)
project(shk)

add_library(shklib
  src/build.h
  src/command_runner.h
  src/file_system.h
  src/fingerprint.cpp
  src/fingerprint.h
  src/hash.h
  src/invocation.h
  src/io_error.h
  src/manifest_parser.h
  src/path.h
  src/step.h
  src/task.h)
set_property(TARGET shklib PROPERTY CXX_STANDARD 11)
target_include_directories(shklib PUBLIC src)

add_executable(shk
  src/shk.cpp)
set_property(TARGET shk PROPERTY CXX_STANDARD 11)
target_link_libraries(shk shklib)

set(RC_ENABLE_CATCH ON)
add_subdirectory(vendor/rapidcheck)

add_executable(shk_test
  test/in_memory_file_system.cpp
  test/in_memory_file_system.h
  test/main.cpp
  test/properties.cpp)
set_property(TARGET shk_test PROPERTY CXX_STANDARD 11)
target_link_libraries(shk_test shklib catch rapidcheck rapidcheck_catch)
