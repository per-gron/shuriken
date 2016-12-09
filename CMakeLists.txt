cmake_minimum_required(VERSION 3.2.1)
project(shk)

set(SHK_COMPILER_FLAGS "-Wall -Wimplicit-fallthrough -Werror")

add_subdirectory(vendor)
add_subdirectory(src/util)
add_subdirectory(src/shk)
