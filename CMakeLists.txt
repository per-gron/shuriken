cmake_minimum_required(VERSION 3.2.1)
project(shk)

enable_testing()

set(SHK_COMPILER_FLAGS "-Wall -Wimplicit-fallthrough -Werror")

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(CMAKE_C_ARCHIVE_CREATE   "<CMAKE_AR> Scr <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> Scr <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_C_ARCHIVE_FINISH   "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
  set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
endif()

add_subdirectory(vendor)
add_subdirectory(src/benchmark)
add_subdirectory(src/rx)
add_subdirectory(src/shk)
add_subdirectory(src/shk-cache)
add_subdirectory(src/shk-trace)
add_subdirectory(src/shk-util)
