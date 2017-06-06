add_library(rs-grpc
  include/rs-grpc/grpc_error.h
  include/rs-grpc/rs_grpc.h
  include/rs-grpc/rs_grpc_flatbuffers.h
  include/rs-grpc/rs_grpc_identity_transform.h
  include/rs-grpc/rs_grpc_tag.h
  src/dummy.cpp)
target_include_directories(rs PUBLIC include)
target_link_libraries(rs-grpc rs flatbuffers grpc++)
set_target_properties(rs-grpc PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET rs-grpc PROPERTY CXX_STANDARD 14)

add_custom_command(
  OUTPUT
      ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest.grpc.fb.cc
      ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest.grpc.fb.h
      ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest_generated.h
  COMMAND flatc --cpp --grpc --scoped-enums -o test/include ${CMAKE_CURRENT_LIST_DIR}/test/rsgrpctest.fbs
  DEPENDS test/rsgrpctest.fbs flatc)

add_executable(rs-grpc-test
  test/main.cpp
  test/rs_grpc_test.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest.grpc.fb.cc
  ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest.grpc.fb.h
  ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest_generated.h)
set_property(TARGET rs-grpc-test PROPERTY CXX_STANDARD 14)
target_link_libraries(rs-grpc-test rs-grpc catch)
target_include_directories(rs-grpc-test PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/test/include src)
set_target_properties(rs-grpc-test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME rs-grpc-unit COMMAND rs-grpc-test)
