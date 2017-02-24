add_custom_command(
  OUTPUT lexer.cpp
  COMMAND re2c -b -i --no-generation-date -o lexer.cpp ${CMAKE_CURRENT_LIST_DIR}/src/manifest/lexer.in.cpp
  DEPENDS src/manifest/lexer.in.cpp re2c)

add_custom_command(
  OUTPUT sandbox_parser.cpp
  COMMAND re2c -b -i --no-generation-date -o sandbox_parser.cpp ${CMAKE_CURRENT_LIST_DIR}/src/cmd/sandbox_parser.in.cpp
  DEPENDS src/cmd/sandbox_parser.in.cpp re2c)

add_library(shklib
  src/build.h
  src/build.cpp
  src/build_config.h
  src/build_error.h
  src/clock.h
  src/cmd/command_runner.h
  src/cmd/dry_run_command_runner.cpp
  src/cmd/dry_run_command_runner.h
  src/cmd/limited_command_runner.cpp
  src/cmd/limited_command_runner.h
  src/cmd/pooled_command_runner.cpp
  src/cmd/pooled_command_runner.h
  src/cmd/real_command_runner.h
  src/cmd/real_command_runner_posix.cpp
  src/cmd/sandbox_parser.h
  src/cmd/tracing_command_runner.cpp
  src/cmd/tracing_command_runner.h
  src/dependency_type.h
  src/edit_distance.cpp
  src/edit_distance.h
  src/exit_status.h
  src/fs/cleaning_file_system.cpp
  src/fs/cleaning_file_system.h
  src/fs/dry_run_file_system.cpp
  src/fs/dry_run_file_system.h
  src/fs/file_id.h
  src/fs/file_lock.cpp
  src/fs/file_lock.h
  src/fs/file_system.cpp
  src/fs/file_system.h
  src/fs/fingerprint.cpp
  src/fs/fingerprint.h
  src/fs/path.cpp
  src/fs/path.h
  src/fs/path_error.h
  src/fs/persistent_file_system.cpp
  src/fs/persistent_file_system.h
  src/hash.h
  src/manifest/eval_env.cpp
  src/manifest/eval_env.h
  src/manifest/eval_string.cpp
  src/manifest/eval_string.h
  src/manifest/indexed_manifest.cpp
  src/manifest/indexed_manifest.h
  src/manifest/lexer.h
  src/manifest/manifest.cpp
  src/manifest/manifest.h
  src/manifest/rule.cpp
  src/manifest/rule.h
  src/manifest/step.cpp
  src/manifest/step.h
  src/io_error.h
  src/log/delayed_invocation_log.cpp
  src/log/delayed_invocation_log.h
  src/log/dry_run_invocation_log.h
  src/log/dummy_invocation_log.h
  src/log/invocation_log.cpp
  src/log/invocation_log.h
  src/log/invocations.h
  src/log/persistent_invocation_log.cpp
  src/log/persistent_invocation_log.h
  src/optional.h
  src/parse_error.h
  src/status/build_status.h
  src/status/dummy_build_status.h
  src/status/line_printer.cpp
  src/status/line_printer.h
  src/status/stopwatch.h
  src/status/terminal_build_status.cpp
  src/status/terminal_build_status.h
  src/string_piece.h
  src/tools/clean.cpp
  src/tools/clean.h
  src/tools/compilation_database.cpp
  src/tools/compilation_database.h
  src/tools/deps.cpp
  src/tools/deps.h
  src/tools/query.cpp
  src/tools/query.h
  src/tools/recompact.cpp
  src/tools/recompact.h
  src/tools/targets.cpp
  src/tools/targets.h
  src/util.cpp
  src/util.h
  src/version.cpp
  src/version.h
  lexer.cpp
  sandbox_parser.cpp)
set_property(TARGET shklib PROPERTY CXX_STANDARD 11)
target_include_directories(shklib PUBLIC src)
target_link_libraries(shklib utf8proc blake2 shkutil)
set_target_properties(shklib PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_executable(shk
  src/shk.cpp)
set_property(TARGET shk PROPERTY CXX_STANDARD 11)
target_link_libraries(shk shklib)
set_target_properties(shk PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_executable(shk_test
  test/build_test.cpp
  test/cmd/dry_run_command_runner_test.cpp
  test/cmd/limited_command_runner_test.cpp
  test/cmd/pooled_command_runner_test.cpp
  test/cmd/real_command_runner_test.cpp
  test/cmd/sandbox_parser_test.cpp
  test/cmd/tracing_command_runner_test.cpp
  test/dummy_command_runner.cpp
  test/dummy_command_runner.h
  test/dummy_command_runner_test.cpp
  test/edit_distance_test.cpp
  test/fs/cleaning_file_system_test.cpp
  test/fs/dry_run_file_system_test.cpp
  test/fs/file_id_test.cpp
  test/fs/file_lock_test.cpp
  test/fs/file_system_test.cpp
  test/fs/fingerprint_test.cpp
  test/fs/path_test.cpp
  test/fs/persistent_file_system_test.cpp
  test/generators.cpp
  test/generators.h
  test/in_memory_file_system.cpp
  test/in_memory_file_system.h
  test/in_memory_file_system_test.cpp
  test/in_memory_invocation_log.cpp
  test/in_memory_invocation_log.h
  test/in_memory_invocation_log_test.cpp
  test/log/delayed_invocation_log_test.cpp
  test/log/persistent_invocation_log_test.cpp
  test/main.cpp
  test/manifest/indexed_manifest_test.cpp
  test/manifest/lexer_test.cpp
  test/manifest/manifest_test.cpp
  test/manifest/step_test.cpp
  test/optional_test.cpp
  test/properties.cpp
  test/status/line_printer_test.cpp
  test/status/stopwatch_test.cpp
  test/util_test.cpp)
set_property(TARGET shk_test PROPERTY CXX_STANDARD 11)
target_include_directories(shk_test PUBLIC "${CMAKE_SOURCE_DIR}/vendor/rapidcheck/extras/catch/include")
target_link_libraries(shk_test shklib catch rapidcheck)
set_target_properties(shk_test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME unit COMMAND shk_test)
add_test(
  NAME integration
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/integrationtest
  COMMAND env SHK_PATH=${CMAKE_CURRENT_BINARY_DIR}/shk python ${CMAKE_CURRENT_LIST_DIR}/integrationtest/test.py)
