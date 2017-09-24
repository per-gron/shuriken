shk_proto(
  LIB_NAME shk-store-proto
  PROTO_FILE shk-store/api/shkstore.proto)

shk_library(
  NAME libshk-store
  SOURCES
    src/dummy.cpp
  DEPENDENCIES
    shk-store-proto)

add_executable(shk-store
  src/shk-store.cpp)
shk_cpp_target(shk-store)
add_sanitizers(shk-store)
target_link_libraries(shk-store libshk-store rs-grpc)
set_target_properties(shk-store PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

add_executable(shk-store-test
  test/main.cpp)
shk_cpp_target(shk-store-test)
target_link_libraries(shk-store-test catch libshk-store)
target_include_directories(shk-store-test PRIVATE src)

add_test(NAME shk-store-unit COMMAND shk-store-test)
