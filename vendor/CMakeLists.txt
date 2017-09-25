add_subdirectory(blake2_cmake)

add_library(catch INTERFACE)
target_include_directories(catch INTERFACE catch/include)

set(gRPC_USE_PROTO_LITE ON CACHE BOOL "Use the protobuf-lite library")
set(BENCHMARK_ENABLE_TESTING OFF)
if(CMAKE_GENERATOR STREQUAL Xcode)
  # The Xcode CMake generator does not properly implement EXCLUDE_FROM_ALL:
  # EXCLUDE_FROM_ALL should mean that it's not part of the all target, but if
  # a target depends on it, it should still be built. What Xcode does it to just
  # not build it, even if a target depends on it. Because there are targets that
  # depend on targetrs in thie subdirectory, EXCLUDE_FROM_ALL breaks on Xcode.
  add_subdirectory(grpc)
else()
  add_subdirectory(grpc EXCLUDE_FROM_ALL)
endif()
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
target_include_directories(grpc++ PUBLIC "${CMAKE_CURRENT_LIST_DIR}/grpc/include")

set(FLATBUFFERS_BUILD_TESTS OFF)
add_subdirectory(flatbuffers)
target_include_directories(flatbuffers PUBLIC "${CMAKE_CURRENT_LIST_DIR}/flatbuffers/include")

# EXCLUDE_FROM_ALL avoids unnecessarily building the lz4 and lz4c programs
add_subdirectory(lz4/contrib/cmake_unofficial EXCLUDE_FROM_ALL)

add_subdirectory(re2c_cmake)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/sanitizers-cmake/cmake" ${CMAKE_MODULE_PATH})
find_package(Sanitizers)

add_subdirectory(googleapis_cmake)
