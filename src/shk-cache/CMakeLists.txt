shk_proto(
  LIB_NAME shk-cache-proto
  PROTO_FILE shk-cache/api/shkcache.proto
  DEPENDENCIES shk-index-proto
  GRPC)

shk_library(
  NAME libshk-cache
  SOURCES
    src/dummy.cpp
  DEPENDENCIES
    shk-cache-proto)

shk_executable(
  NAME shk-cache
  SOURCES
    src/shk-cache.cpp
  DEPENDENCIES
    libshk-cache
  USER_FACING)

shk_test(
  NAME shk-cache-test
  SOURCES
    test/main.cpp
  DEPENDENCIES
    catch libshk-cache
  PRIVATE_INCLUDES
    src)
