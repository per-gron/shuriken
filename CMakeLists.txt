cmake_minimum_required(VERSION 3.2.1)
project(shk)

add_executable(shk src/shk.cpp)
set_property(TARGET shk PROPERTY CXX_STANDARD 11)
