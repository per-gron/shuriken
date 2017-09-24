shk_proto(
  LIB_NAME shk-index-proto
  PROTO_FILE shk-index/api/shkindex.proto)

shk_library(
  NAME libshk-index
  SOURCES
    src/dummy.cpp
  DEPENDENCIES
    shk-index-proto)

shk_executable(
  NAME shk-index
  SOURCES
    src/shk-index.cpp
  DEPENDENCIES
    libshk-index rs-grpc
  USER_FACING)

shk_test(
  NAME shk-index-test
  SOURCES
    test/main.cpp
  DEPENDENCIES
    catch libshk-index
  PRIVATE_INCLUDES
    src)
