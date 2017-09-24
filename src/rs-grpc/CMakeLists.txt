shk_library(
  NAME rs-grpc
  SOURCES
    include/rs-grpc/detail/rs_grpc_tag.h
    include/rs-grpc/detail/subscriber.h
    include/rs-grpc/detail/subscription.h
    include/rs-grpc/call_context.h
    include/rs-grpc/client.h
    include/rs-grpc/grpc_error.h
    include/rs-grpc/server.h
    src/rs_grpc_tag.cpp
  DEPENDENCIES
    rs grpc++
  PUBLIC_INCLUDES include)

add_custom_command(
  OUTPUT
      ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest.pb.cc
      ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest.pb.h
  COMMAND protoc --cpp_out=test/include --proto_path=${CMAKE_CURRENT_LIST_DIR}/test ${CMAKE_CURRENT_LIST_DIR}/test/rsgrpctest.proto
  DEPENDS test/rsgrpctest.proto protoc)

add_custom_command(
  OUTPUT
      ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest.grpc.pb.cc
      ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest.grpc.pb.h
  COMMAND protoc --grpc_out=test/include --plugin=protoc-gen-grpc="$<TARGET_FILE:grpc_cpp_plugin>" --proto_path=${CMAKE_CURRENT_LIST_DIR}/test ${CMAKE_CURRENT_LIST_DIR}/test/rsgrpctest.proto
  DEPENDS test/rsgrpctest.proto protoc grpc_cpp_plugin)

add_executable(rs-grpc-test
  test/detail/rs_grpc_tag_test.cpp
  test/detail/subscription_test.cpp
  test/detail/subscriber_test.cpp
  test/bidi_streaming_test.cpp
  test/client_streaming_test.cpp
  test/main.cpp
  test/server_streaming_test.cpp
  test/test_util.cpp
  test/test_util.h
  test/unary_test.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest.grpc.pb.cc
  ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest.grpc.pb.h
  ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest.pb.cc
  ${CMAKE_CURRENT_BINARY_DIR}/test/include/rsgrpctest.pb.h)
add_sanitizers(rs-grpc-test)
set_property(TARGET rs-grpc-test PROPERTY CXX_STANDARD 14)
target_link_libraries(rs-grpc-test rs-grpc catch libprotobuf)
target_include_directories(rs-grpc-test PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/test/include src)
set_property(TARGET rs-grpc-test APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME rs-grpc-unit COMMAND rs-grpc-test)
