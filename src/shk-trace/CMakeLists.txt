# This is a hack workaround for the fact that CMake+Ninja doesn't respect
# the USES_TERMINAL keyword when it's applied to the add_custom_command
# declarations below.
add_custom_command(
  OUTPUT sudo_dummy.h
  COMMAND sudo echo > /dev/null
  VERBATIM USES_TERMINAL)

add_library(libshktrace
  src/cmdline_options.cpp
  src/cmdline_options.h
  src/cwd_memo.cpp
  src/cwd_memo.h
  src/daemon.cpp
  src/daemon.h
  src/dispatch.h
  src/event.cpp
  src/event.h
  src/event_consolidator.cpp
  src/event_consolidator.h
  src/event_info.h
  src/file_descriptor.cpp
  src/file_descriptor.h
  src/file_descriptor_memo.cpp
  src/file_descriptor_memo.h
  src/fileport.h
  src/kdebug.h
  src/kdebug_controller.cpp
  src/kdebug_controller.h
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
  src/tracer.cpp
  src/tracer.h
  src/tracing_server.cpp
  src/tracing_server.h)
target_compile_definitions(libshktrace PUBLIC KERNEL_PRIVATE PRIVATE=)
target_link_libraries(libshktrace util bsm shkutil)
set_property(TARGET libshktrace PROPERTY CXX_STANDARD 11)

add_executable(shk-trace
  src/main.cpp
  sudo_dummy.h)
target_link_libraries(shk-trace libshktrace)
set_property(TARGET shk-trace PROPERTY CXX_STANDARD 11)

add_executable(shktrace_test
  test/cmdline_options_test.cpp
  test/cwd_memo_test.cpp
  test/event_consolidator_test.cpp
  test/event_test.cpp
  test/file_descriptor_memo_test.cpp
  test/mach_port_test.cpp
  test/main.cpp
  test/mock_tracer_delegate.h
  test/named_mach_port_test.cpp
  test/path_resolver_test.cpp
  test/process_tracer_test.cpp
  test/tracing_server_test.cpp)
target_link_libraries(shktrace_test pthread catch libshktrace)
target_include_directories(shktrace_test PUBLIC src)
set_property(TARGET shktrace_test PROPERTY CXX_STANDARD 11)

add_custom_command(
  TARGET shk-trace
  PRE_LINK
  COMMAND rm -f shk-trace
  VERBATIM USES_TERMINAL)
add_custom_command(
  TARGET shk-trace
  POST_BUILD
  COMMAND sudo chown root shk-trace
  COMMAND sudo chmod u+s shk-trace
  VERBATIM USES_TERMINAL)

add_test(NAME trace_unit COMMAND shktrace_test)
add_test(
  NAME trace_integration
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/integrationtest
  COMMAND env SHK_TRACE_PATH=${CMAKE_CURRENT_BINARY_DIR}/shk-trace python ${CMAKE_CURRENT_LIST_DIR}/integrationtest/test.py)
