cmake_minimum_required(VERSION 3.2.1)
project(shk)

add_executable(shk src/shk.cpp)
set_property(TARGET shk PROPERTY CXX_STANDARD 11)

set(RC_ENABLE_CATCH ON)
add_subdirectory(vendor/rapidcheck)

add_library(shk_catch INTERFACE)
target_link_libraries(shk_catch INTERFACE rapidcheck)
target_include_directories(shk_catch INTERFACE vendor/catch)

add_executable(shk_test test/main.cpp)
set_property(TARGET shk_test PROPERTY CXX_STANDARD 11)
target_link_libraries(shk_test shk_catch rapidcheck rapidcheck_catch)
