add_custom_command(
  OUTPUT
      ${CMAKE_CURRENT_BINARY_DIR}/include/shk-cache/shkcache.grpc.fb.cc
      ${CMAKE_CURRENT_BINARY_DIR}/include/shk-cache/shkcache.grpc.fb.h
      ${CMAKE_CURRENT_BINARY_DIR}/include/shk-cache/shkcache_generated.h
  COMMAND flatc --cpp --grpc --scoped-enums -o include/shk-cache ${CMAKE_CURRENT_LIST_DIR}/include/shk-cache/shkcache.fbs
  DEPENDS include/shk-cache/shkcache.fbs flatc)

add_library(libshk-cache
  src/dummy.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/include/shk-cache/shkcache.grpc.fb.cc
  ${CMAKE_CURRENT_BINARY_DIR}/include/shk-cache/shkcache.grpc.fb.h
  ${CMAKE_CURRENT_BINARY_DIR}/include/shk-cache/shkcache_generated.h)
set_target_properties(libshk-cache PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET libshk-cache PROPERTY CXX_STANDARD 11)
target_include_directories(libshk-cache PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(libshk-cache flatbuffers grpc++ zlib)

add_executable(shk-cache
  src/shk-cache.cpp)
set_target_properties(shk-cache PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET shk-cache PROPERTY CXX_STANDARD 11)
target_link_libraries(shk-cache libshk-cache)
set_target_properties(shk-cache PROPERTIES
  COMPILE_FLAGS ${SHK_COMPILER_FLAGS}
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

add_executable(shk-cache-test
  test/main.cpp)
set_property(TARGET shk-cache-test PROPERTY CXX_STANDARD 11)
target_link_libraries(shk-cache-test catch libshk-cache)
set_target_properties(shk-cache-test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME shk-cache-unit COMMAND shk-cache-test)
