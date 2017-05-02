add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/include/shk-trace/debug_capture_log_generated.h
  COMMAND flatc --binary --cpp --scoped-enums -o include/shk-trace ${CMAKE_CURRENT_LIST_DIR}/src/debug_capture_log.fbs
  DEPENDS src/debug_capture_log.fbs flatc)

add_library(libshktrace
  src/apsl_code.cpp
  src/apsl_code.h
  src/cmdline_options.cpp
  src/cmdline_options.h
  src/cwd_memo.cpp
  src/cwd_memo.h
  src/daemon.cpp
  src/daemon.h
  src/debug_capture_log.cpp
  src/debug_capture_log.h
  src/dispatch.h
  src/event_consolidator.cpp
  src/event_consolidator.h
  src/event_info_map.cpp
  src/event_info_map.h
  src/event_type.h
  src/file_descriptor_memo.cpp
  src/file_descriptor_memo.h
  src/fileport.h
  src/kdebug.h
  src/kdebug_controller.cpp
  src/kdebug_controller.h
  src/kdebug_pump.cpp
  src/kdebug_pump.h
  src/mach_port.cpp
  src/mach_port.h
  src/named_mach_port.cpp
  src/named_mach_port.h
  src/path_resolver.cpp
  src/path_resolver.h
  src/process_tracer.cpp
  src/process_tracer.h
  src/syscall_constants.h
  src/syscall_tables.h
  src/to_json.cpp
  src/to_json.h
  src/trace_writer.cpp
  src/trace_writer.h
  src/tracer.cpp
  src/tracer.h
  src/tracing_server.cpp
  src/tracing_server.h
  ${CMAKE_CURRENT_BINARY_DIR}/include/shk-trace/debug_capture_log_generated.h)
target_compile_definitions(libshktrace PUBLIC KERNEL_PRIVATE PRIVATE=)
target_include_directories(libshktrace PUBLIC
    src
    ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(libshktrace util bsm shkutil)
set_target_properties(libshktrace PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET libshktrace PROPERTY CXX_STANDARD 11)

add_executable(shk-trace-nosuid
  src/main.cpp)
target_link_libraries(shk-trace-nosuid libshktrace)
set_target_properties(shk-trace-nosuid PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set_property(TARGET shk-trace-nosuid PROPERTY CXX_STANDARD 11)

add_executable(shktrace_test
  test/cmdline_options_test.cpp
  test/cwd_memo_test.cpp
  test/debug_capture_log_test.cpp
  test/event_consolidator_test.cpp
  test/event_info_map_test.cpp
  test/file_descriptor_memo_test.cpp
  test/mach_port_test.cpp
  test/main.cpp
  test/mock_tracer_delegate.h
  test/named_mach_port_test.cpp
  test/path_resolver_test.cpp
  test/process_tracer_test.cpp
  test/to_json_test.cpp
  test/trace_writer_test.cpp
  test/tracer_test.cpp
  test/tracing_server_test.cpp)
target_link_libraries(shktrace_test pthread catch libshktrace)
set_target_properties(shktrace_test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET shktrace_test PROPERTY CXX_STANDARD 11)

add_executable(shktrace_integrationtest_helper
  integrationtest/test.cpp)
target_link_libraries(shktrace_integrationtest_helper shkutil pthread)
set_target_properties(shktrace_integrationtest_helper PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
# For some reason, calls to __guarded_open_dprotected_np and __guarded_open_np
# seem to misbehave when optimization is enabled, so pass -O0. This program is
# just a test helper anyway so it doesn't need optimization.
set_target_properties(shktrace_integrationtest_helper PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations -O0")
set_property(TARGET shktrace_integrationtest_helper PROPERTY CXX_STANDARD 11)

add_custom_target(shk-trace
  COMMAND "${PROJECT_SOURCE_DIR}/scripts/make_suid.sh" "$<TARGET_FILE:shk-trace-nosuid>"
  DEPENDS shk-trace-nosuid
  VERBATIM USES_TERMINAL)

add_test(NAME trace_unit COMMAND shktrace_test)
add_test(
  NAME trace_integration
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/integrationtest
  COMMAND env SHK_TRACE_FB_PATH=${CMAKE_CURRENT_BINARY_DIR}/../shkutil/include/util SHK_TRACE_PATH=${PROJECT_BINARY_DIR}/bin/shk-trace SHK_TRACE_TEST_HELPER_PATH=${CMAKE_CURRENT_BINARY_DIR}/shktrace_integrationtest_helper python ${CMAKE_CURRENT_LIST_DIR}/integrationtest/test.py)
