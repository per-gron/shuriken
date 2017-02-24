cmake_minimum_required(VERSION 3.2.1)
project(shk-exec)

# This is a hack workaround for the fact that CMake+Ninja doesn't respect
# the USES_TERMINAL keyword when it's applied to the add_custom_command
# declarations below.
add_custom_command(
  OUTPUT sudo_dummy.h
  COMMAND sudo echo > /dev/null && touch sudo_dummy.h
  VERBATIM USES_TERMINAL)

add_executable(shk-exec
  src/fs_usage.cpp
  sudo_dummy.h)
target_compile_definitions(shk-exec PRIVATE KERNEL_PRIVATE PRIVATE=)
target_link_libraries(shk-exec util)
set_property(TARGET shk-exec PROPERTY CXX_STANDARD 11)

add_custom_command(
  TARGET shk-exec
  PRE_LINK
  COMMAND rm -f shk-exec
  VERBATIM USES_TERMINAL)
add_custom_command(
  TARGET shk-exec
  POST_BUILD
  COMMAND sudo chown root shk-exec
  COMMAND sudo chmod u+s shk-exec
  VERBATIM USES_TERMINAL)
