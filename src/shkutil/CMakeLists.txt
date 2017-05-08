add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/include/util/shktrace_generated.h
  COMMAND flatc --binary --cpp --python --scoped-enums -o include/util ${CMAKE_CURRENT_LIST_DIR}/src/shktrace.fbs
  DEPENDS src/shktrace.fbs flatc)

add_library(shkutil
  include/util/file_descriptor.h
  include/util/path_error.h
  include/util/path_operations.h
  include/util/raii_helper.h
  include/util/shktrace.h
  include/util/string_view.h
  src/file_descriptor.cpp
  src/path_operations.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/include/util/shktrace_generated.h)
target_include_directories(shkutil PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/include)
set_target_properties(shkutil PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
target_link_libraries(shkutil flatbuffers)
set_property(TARGET shkutil PROPERTY CXX_STANDARD 11)

add_executable(shkutil_test
  test/main.cpp
  test/path_operations_test.cpp
  test/raii_helper_test.cpp
  test/shktrace_test.cpp
  test/string_view_test.cpp)
set_property(TARGET shkutil_test PROPERTY CXX_STANDARD 11)
target_link_libraries(shkutil_test shkutil catch)
set_target_properties(shkutil_test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME util_unit COMMAND shkutil_test)
