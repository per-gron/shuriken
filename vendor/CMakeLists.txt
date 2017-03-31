add_subdirectory(blake2_cmake)

add_subdirectory(flatbuffers)
target_include_directories(flatbuffers PUBLIC include ${CMAKE_CURRENT_LIST_DIR}/flatbuffers/include)

set(RC_ENABLE_CATCH ON)
add_subdirectory(rapidcheck)

add_subdirectory(re2c_cmake)
