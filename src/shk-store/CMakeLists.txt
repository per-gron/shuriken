add_library(libshk-store
  src/dummy.cpp)
set_property(TARGET libshk-store APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET libshk-store PROPERTY CXX_STANDARD 14)
target_include_directories(libshk-store PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(libshk-store)

add_executable(shk-store
  src/shk-store.cpp)
set_property(TARGET shk-store APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET shk-store PROPERTY CXX_STANDARD 14)
target_link_libraries(shk-store libshk-store)
set_target_properties(shk-store PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

add_executable(shk-store-test
  test/main.cpp)
set_property(TARGET shk-store-test PROPERTY CXX_STANDARD 14)
target_link_libraries(shk-store-test catch libshk-store)
target_include_directories(shk-store-test PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/test/include src)
set_property(TARGET shk-store-test APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME shk-store-unit COMMAND shk-store-test)
