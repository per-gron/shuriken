add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/include/shk-cache/shkcache_generated.h
  COMMAND flatc --binary --cpp --grpc --scoped-enums -o include/shk-cache ${CMAKE_CURRENT_LIST_DIR}/include/shk-cache/shkcache.fbs
  DEPENDS include/shk-cache/shkcache.fbs flatc)

add_library(shk-cachelib
  src/dummy.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/include/shk-cache/shkcache_generated.h)
target_include_directories(shk-cachelib PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/include)
set_target_properties(shk-cachelib PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
target_link_libraries(shk-cachelib flatbuffers)
set_property(TARGET shk-cachelib PROPERTY CXX_STANDARD 11)

add_executable(shk-cachelib_test
  test/main.cpp)
set_property(TARGET shk-cachelib_test PROPERTY CXX_STANDARD 11)
target_link_libraries(shk-cachelib_test shkutil catch)
set_target_properties(shk-cachelib_test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME shk-cache_unit COMMAND shk-cachelib_test)
