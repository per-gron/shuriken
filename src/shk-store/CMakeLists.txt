shk_proto(
  LIB_NAME shk-store-proto
  PROTO_FILE shk-store/api/shkstore.proto)

shk_library(
  NAME libshk-store
  SOURCES
    src/dummy.cpp
  DEPENDENCIES
    shk-store-proto)

shk_executable(
  NAME shk-store
  SOURCES
    src/shk-store.cpp
  DEPENDENCIES
    libshk-store rs-grpc google-bigtable-v2-proto
  USER_FACING)

shk_test(
  NAME shk-store-test
  SOURCES
    test/main.cpp
  DEPENDENCIES
    catch libshk-store
  PRIVATE_INCLUDES
    src)
