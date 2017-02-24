cmake_minimum_required(VERSION 3.2.1)
project(shk)

enable_testing()

set(SHK_COMPILER_FLAGS "-Wall -Wimplicit-fallthrough -Werror")

add_subdirectory(vendor)
add_subdirectory(src/shkutil)
add_subdirectory(src/shk)
add_subdirectory(src/shk-trace)
