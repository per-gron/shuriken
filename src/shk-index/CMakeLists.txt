shk_proto(
  LIB_NAME shk-index-proto
  PROTO_FILE shk-index/api/shkindex.proto)

add_library(libshk-index
  src/dummy.cpp)
shk_cpp_target(libshk-index)
target_include_directories(libshk-index PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(libshk-index shk-index-proto)

add_executable(shk-index
  src/shk-index.cpp)
shk_cpp_target(shk-index)
add_sanitizers(shk-index)
target_link_libraries(shk-index libshk-index rs-grpc)
set_target_properties(shk-index PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

add_executable(shk-index-test
  test/main.cpp)
shk_cpp_target(shk-index-test)
target_link_libraries(shk-index-test catch libshk-index)
target_include_directories(shk-index-test PRIVATE src)

add_test(NAME shk-index-unit COMMAND shk-index-test)
