shk_proto(
  LIB_NAME shk-cache-proto
  PROTO_FILE shk-cache/api/shkcache.proto
  DEPENDENCIES shk-index-proto)

add_library(libshk-cache
  src/dummy.cpp)
set_property(TARGET libshk-cache APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET libshk-cache PROPERTY CXX_STANDARD 14)
target_include_directories(libshk-cache PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(libshk-cache shk-cache-proto)

add_executable(shk-cache
  src/shk-cache.cpp)
add_sanitizers(shk-cache)
set_property(TARGET shk-cache APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET shk-cache PROPERTY CXX_STANDARD 14)
target_link_libraries(shk-cache libshk-cache)
set_target_properties(shk-cache PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

add_executable(shk-cache-test
  test/main.cpp)
set_property(TARGET shk-cache-test PROPERTY CXX_STANDARD 14)
target_link_libraries(shk-cache-test catch libshk-cache)
target_include_directories(shk-cache-test PRIVATE src)
set_property(TARGET shk-cache-test APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME shk-cache-unit COMMAND shk-cache-test)
