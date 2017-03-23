add_library(shkutil
  include/util/file_descriptor.h
  include/util/raii_helper.h
  src/file_descriptor.cpp)
target_include_directories(shkutil PUBLIC include)
set_property(TARGET shkutil PROPERTY CXX_STANDARD 11)

add_executable(shkutil_test
  test/main.cpp
  test/raii_helper_test.cpp)
set_property(TARGET shkutil_test PROPERTY CXX_STANDARD 11)
target_link_libraries(shkutil_test shkutil catch)
set_target_properties(shkutil_test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME util_unit COMMAND shkutil_test)
