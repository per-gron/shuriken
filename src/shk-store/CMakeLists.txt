shk_proto(
  LIB_NAME shk-store-proto
  PROTO_FILE shk-store/api/shkstore.proto)

add_library(libshk-store
  src/dummy.cpp)
shk_cpp_target(libshk-store)
target_include_directories(libshk-store PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(libshk-store shk-store-proto)

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
