add_subdirectory(blake2_cmake)

set(FLATBUFFERS_BUILD_TESTS OFF)
add_subdirectory(flatbuffers)
target_include_directories(flatbuffers PUBLIC include ${CMAKE_CURRENT_LIST_DIR}/flatbuffers/include)

add_library(catch INTERFACE)
target_include_directories(catch INTERFACE catch/include)

set(gRPC_USE_PROTO_LITE ON CACHE BOOL "Use the protobuf-lite library")
set(BENCHMARK_ENABLE_TESTING OFF)
add_subdirectory(grpc EXCLUDE_FROM_ALL)
foreach(LIBRARY zlib libprotobuf)
  target_compile_definitions(${LIBRARY} PUBLIC z_size_t=size_t)
endforeach()
foreach(LIBRARY libprotobuf-lite libprotobuf protoc libprotoc)
  set_target_properties(${LIBRARY} PROPERTIES
    COMPILE_OPTIONS "-Wno-deprecated-declarations")
endforeach()
set_target_properties(cares PROPERTIES
  COMPILE_OPTIONS "-Wno-invalid-source-encoding")
# Needs to make sure to build example because it is needed for a zlib ctest that can't be configured away afaik
add_custom_target(build_example ALL DEPENDS example)

add_subdirectory(re2c_cmake)
