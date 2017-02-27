# This is a hack workaround for the fact that CMake+Ninja doesn't respect
# the USES_TERMINAL keyword when it's applied to the add_custom_command
# declarations below.
add_custom_command(
  OUTPUT sudo_dummy.h
  COMMAND sudo echo > /dev/null
  VERBATIM USES_TERMINAL)

set(shktrace_definitions KERNEL_PRIVATE PRIVATE=)

add_library(libshktrace
  src/daemon.cpp
  src/daemon.h
  src/event_info.h
  src/kdebug.h
  src/socket.cpp
  src/socket.h
  src/syscall_constants.h
  src/syscall_tables.h
  src/sysctl_helpers.cpp
  src/sysctl_helpers.h
  src/tracer.cpp
  src/tracer.h)
target_compile_definitions(libshktrace PRIVATE ${shktrace_definitions})
set_property(TARGET libshktrace PROPERTY CXX_STANDARD 11)

add_executable(shk-trace
  src/main.cpp
  sudo_dummy.h)
target_compile_definitions(shk-trace PRIVATE ${shktrace_definitions})
target_link_libraries(shk-trace util libshktrace)
set_property(TARGET shk-trace PROPERTY CXX_STANDARD 11)

add_executable(shktrace_test
  test/main.cpp
  test/socket_test.cpp)
target_link_libraries(shktrace_test catch libshktrace)
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
