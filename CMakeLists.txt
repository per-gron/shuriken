cmake_minimum_required(VERSION 3.2.1)
project(shk-trace)

# This is a hack workaround for the fact that CMake+Ninja doesn't respect
# the USES_TERMINAL keyword when it's applied to the add_custom_command
# declarations below.
add_custom_command(
  OUTPUT sudo_dummy.h
  COMMAND sudo echo > /dev/null
  VERBATIM USES_TERMINAL)

add_executable(shk-trace
  src/daemon.cpp
  src/main.cpp
  src/sysctl_helpers.cpp
  sudo_dummy.h)
target_compile_definitions(shk-trace PRIVATE KERNEL_PRIVATE PRIVATE=)
target_link_libraries(shk-trace util)
set_property(TARGET shk-trace PROPERTY CXX_STANDARD 11)

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
