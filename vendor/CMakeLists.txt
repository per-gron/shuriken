add_subdirectory(blake2_cmake)

set(FLATBUFFERS_BUILD_TESTS OFF)
add_subdirectory(flatbuffers)
target_include_directories(flatbuffers PUBLIC include ${CMAKE_CURRENT_LIST_DIR}/flatbuffers/include)

add_library(catch INTERFACE)
target_include_directories(catch INTERFACE catch/include)

add_subdirectory(re2c_cmake)
