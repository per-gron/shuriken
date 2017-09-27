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
    include/rs-grpc/service.h
    src/rs_grpc_tag.cpp
    src/service.cpp
  DEPENDENCIES
    rs grpc++
  PUBLIC_INCLUDES include)

shk_proto(
  LIB_NAME rs-grpc-test-proto
  PROTO_FILE rs-grpc/test/rsgrpctest.proto
  GRPC)

shk_executable(
  NAME rs_grpc_plugin
  SOURCES
    src/plugin/plugin.cpp
    src/plugin/generator.cpp
    src/plugin/generator.h
  DEPENDENCIES
    libprotobuf
    libprotoc
  PRIVATE_INCLUDES
    ../../vendor/grpc
    ../../vendor/grpc/include)

shk_test(
  NAME rs-grpc-test
  SOURCES
    test/detail/rs_grpc_tag_test.cpp
    test/detail/subscription_test.cpp
    test/detail/subscriber_test.cpp
    test/bidi_streaming_test.cpp
    test/client_streaming_test.cpp
    test/grpc_error_test.cpp
    test/main.cpp
    test/server_streaming_test.cpp
    test/test_util.cpp
    test/test_util.h
    test/unary_test.cpp
  DEPENDENCIES
    rs-grpc catch rs-grpc-test-proto
  PRIVATE_INCLUDES
    ${CMAKE_CURRENT_BINARY_DIR}/test/include src)
