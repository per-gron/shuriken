cmake_minimum_required(VERSION 3.2.1)
project(shk)

set(SHK_COMPILER_FLAGS "-Wall -Werror")

add_subdirectory(vendor)

add_custom_command(
  OUTPUT lexer.cpp
  COMMAND re2c -b -i --no-generation-date -o lexer.cpp ${CMAKE_SOURCE_DIR}/src/lexer.in.cpp
  DEPENDS src/lexer.in.cpp re2c)

add_library(shklib
  src/build.h
  src/build.cpp
  src/build_status.h
  src/command_runner.h
  src/edit_distance.cpp
  src/edit_distance.h
  src/eval_env.cpp
  src/eval_env.h
  src/file_system.h
  src/fingerprint.cpp
  src/fingerprint.h
  src/hash.h
  src/invocation_log.h
  src/invocations.h
  src/lexer.h
  src/line_printer.cpp
  src/line_printer.h
  src/io_error.h
  src/manifest.h
  src/parse_error.h
  src/path.cpp
  src/path.h
  src/path_error.h
  src/persistent_file_system.cpp
  src/persistent_file_system.h
  src/step.h
  src/string_piece.h
  src/task.h
  src/util.cpp
  src/util.h
  src/version.cpp
  src/version.h
  lexer.cpp)
set_property(TARGET shklib PROPERTY CXX_STANDARD 11)
target_include_directories(shklib PUBLIC src)
set_target_properties(shklib PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_executable(shk
  src/shk.cpp)
set_property(TARGET shk PROPERTY CXX_STANDARD 11)
target_link_libraries(shk shklib)
set_target_properties(shk PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_executable(shk_test
  test/dummy_command_runner.cpp
  test/dummy_command_runner.h
  test/dummy_command_runner_test.cpp
  test/edit_distance_test.cpp
  test/generators.cpp
  test/generators.h
  test/in_memory_file_system.cpp
  test/in_memory_file_system.h
  test/in_memory_file_system_test.cpp
  test/in_memory_invocation_log.cpp
  test/in_memory_invocation_log.h
  test/lexer_test.cpp
  test/line_printer_test.cpp
  test/main.cpp
  test/path_test.cpp
  test/properties.cpp
  test/util_test.cpp)
set_property(TARGET shk_test PROPERTY CXX_STANDARD 11)
target_include_directories(shklib PUBLIC vendor/rapidcheck/extras/catch/include)
target_link_libraries(shk_test shklib catch rapidcheck rapidcheck_catch)
set_target_properties(shk_test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
