add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/include/shk-cache/shkcache_generated.h
  COMMAND flatc --binary --cpp --grpc --scoped-enums -o include/shk-cache ${CMAKE_CURRENT_LIST_DIR}/include/shk-cache/shkcache.fbs
  DEPENDS include/shk-cache/shkcache.fbs flatc)

add_library(libshk-cache
  src/dummy.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/include/shk-cache/shkcache_generated.h)
target_include_directories(libshk-cache PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/include)
set_target_properties(libshk-cache PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
target_link_libraries(libshk-cache flatbuffers)
set_property(TARGET libshk-cache PROPERTY CXX_STANDARD 11)

add_executable(libshk-cache-test
  test/main.cpp)
set_property(TARGET libshk-cache-test PROPERTY CXX_STANDARD 11)
target_link_libraries(libshk-cache-test shk-util catch)
set_target_properties(libshk-cache-test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME shk-cache-unit COMMAND libshk-cache-test)
