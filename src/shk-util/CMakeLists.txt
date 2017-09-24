add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/include/util/shktrace_generated.h
  COMMAND flatc --binary --cpp --python --scoped-enums -o include/util ${CMAKE_CURRENT_LIST_DIR}/src/shktrace.fbs
  DEPENDS src/shktrace.fbs flatc)

shk_library(
  NAME shk-util
  SOURCES
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
    ${CMAKE_CURRENT_BINARY_DIR}/include/util/shktrace_generated.h
  DEPENDENCIES
    flatbuffers
  PUBLIC_INCLUDES
    include ${CMAKE_CURRENT_BINARY_DIR}/include)

shk_test(
  NAME shk-util-test
  SOURCES
    test/assert_test.cpp
    test/main.cpp
    test/path_operations_test.cpp
    test/raii_helper_test.cpp
    test/shktrace_test.cpp
    test/string_view_test.cpp
  DEPENDENCIES
    shk-util catch)
