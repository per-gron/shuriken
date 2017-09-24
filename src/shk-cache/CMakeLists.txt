shk_proto(
  LIB_NAME shk-cache-proto
  PROTO_FILE shk-cache/api/shkcache.proto
  DEPENDENCIES shk-index-proto)

shk_library(
  NAME libshk-cache
  SOURCES
    src/dummy.cpp
  DEPENDENCIES
    shk-cache-proto)

add_executable(shk-cache
  src/shk-cache.cpp)
shk_cpp_target(shk-cache)
add_sanitizers(shk-cache)
target_link_libraries(shk-cache libshk-cache)
set_target_properties(shk-cache PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

add_executable(shk-cache-test
  test/main.cpp)
shk_cpp_target(shk-cache-test)
target_link_libraries(shk-cache-test catch libshk-cache)
target_include_directories(shk-cache-test PRIVATE src)

add_test(NAME shk-cache-unit COMMAND shk-cache-test)
