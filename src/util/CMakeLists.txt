add_library(util INTERFACE)
target_include_directories(util INTERFACE include)

add_executable(util_test
  test/main.cpp
  test/raii_helper_test.cpp)
set_property(TARGET util_test PROPERTY CXX_STANDARD 11)
target_link_libraries(util_test util catch)
set_target_properties(util_test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
