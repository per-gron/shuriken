add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/include/util/shktrace_generated.h
  COMMAND flatc --binary --cpp --python --scoped-enums -o include/util ${CMAKE_CURRENT_LIST_DIR}/src/shktrace.fbs
  DEPENDS src/shktrace.fbs flatc)

add_library(shk-util
  include/util/assert.h
  include/util/file_descriptor.h
  include/util/intrinsics.h
  include/util/path_error.h
  include/util/path_operations.h
  include/util/raii_helper.h
  include/util/shktrace.h
  include/util/string_view.h
  src/assert.cpp
  src/file_descriptor.cpp
  src/path_operations.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/include/util/shktrace_generated.h)
target_include_directories(shk-util PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/include)
set_target_properties(shk-util PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
target_link_libraries(shk-util flatbuffers)
set_property(TARGET shk-util PROPERTY CXX_STANDARD 11)

add_executable(shk-util-test
  test/assert_test.cpp
  test/main.cpp
  test/path_operations_test.cpp
  test/raii_helper_test.cpp
  test/shktrace_test.cpp
  test/string_view_test.cpp)
set_property(TARGET shk-util-test PROPERTY CXX_STANDARD 11)
target_link_libraries(shk-util-test shk-util catch)
set_target_properties(shk-util-test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME shk-util-unit COMMAND shk-util-test)
