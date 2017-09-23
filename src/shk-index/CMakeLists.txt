shk_proto(
  LIB_NAME shk-index-proto
  PROTO_FILE shk-index/api/shkindex.proto)

add_library(libshk-index
  src/dummy.cpp)
set_property(TARGET libshk-index APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET libshk-index PROPERTY CXX_STANDARD 14)
target_include_directories(libshk-index PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(libshk-index shk-index-proto)

add_executable(shk-index
  src/shk-index.cpp)
add_sanitizers(shk-index)
set_property(TARGET shk-index APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET shk-index PROPERTY CXX_STANDARD 14)
target_link_libraries(shk-index libshk-index rs-grpc)
set_target_properties(shk-index PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

add_executable(shk-index-test
  test/main.cpp)
set_property(TARGET shk-index-test PROPERTY CXX_STANDARD 14)
target_link_libraries(shk-index-test catch libshk-index)
target_include_directories(shk-index-test PRIVATE src)
set_property(TARGET shk-index-test APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME shk-index-unit COMMAND shk-index-test)
