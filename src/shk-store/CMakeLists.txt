shk_proto(
  LIB_NAME shk-store-proto
  PROTO_FILE shk-store/api/shkstore.proto
  GRPC)

shk_library(
  NAME libshk-store
  SOURCES
    src/constants.h
    src/store_server.cpp
    src/store_server.h
  DEPENDENCIES
    google-bigtable-v2-proto
    rs
    rs-grpc
    shk-store-proto
    shk-util)

shk_executable(
  NAME shk-store
  SOURCES
    src/shk-store.cpp
  DEPENDENCIES
    libshk-store
  USER_FACING)

shk_test(
  NAME shk-store-test
  SOURCES
    test/main.cpp
  DEPENDENCIES
    catch libshk-store
  PRIVATE_INCLUDES
    src)
