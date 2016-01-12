cmake_minimum_required(VERSION 3.2.1)
project(shk)

add_executable(shk
  src/build.h
  src/command_runner.h
  src/file_system.h
  src/fingerprint.cpp
  src/fingerprint.h
  src/invocation.h
  src/manifest_parser.h
  src/shk.cpp
  src/step.h
  src/task.h)
set_property(TARGET shk PROPERTY CXX_STANDARD 11)

set(RC_ENABLE_CATCH ON)
add_subdirectory(vendor/rapidcheck)

add_executable(shk_test
  test/main.cpp
  test/properties.cpp)
set_property(TARGET shk_test PROPERTY CXX_STANDARD 11)
target_link_libraries(shk_test catch rapidcheck rapidcheck_catch)
