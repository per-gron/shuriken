cmake_minimum_required(VERSION 3.2.1)
project(shk)

add_executable(shk src/shk.cpp)
set_property(TARGET shk PROPERTY CXX_STANDARD 11)

add_executable(shk_test test/main.cpp)
set_property(TARGET shk_test PROPERTY CXX_STANDARD 11)
set_target_properties(shk_test PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/catch")
