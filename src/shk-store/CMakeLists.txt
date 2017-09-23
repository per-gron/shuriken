function(shk_proto LIB_NAME INCLUDE_DIR PROTO_FILE)
  get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
  get_filename_component(PROTO_DIR ${PROTO_FILE} DIRECTORY)
  add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_DIR}/${PROTO_NAME}.pb.cc
        ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_DIR}/${PROTO_NAME}.pb.h
    COMMAND protoc --cpp_out="${PROTO_DIR}" --proto_path="${CMAKE_CURRENT_LIST_DIR}/${PROTO_DIR}" "${CMAKE_CURRENT_LIST_DIR}/${PROTO_FILE}"
    DEPENDS ${PROTO_FILE} protoc)

  add_library(${LIB_NAME}
    ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_DIR}/${PROTO_NAME}.pb.cc
    ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_DIR}/${PROTO_NAME}.pb.h)
  set_property(TARGET ${LIB_NAME} APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
  set_property(TARGET ${LIB_NAME} PROPERTY CXX_STANDARD 14)
  target_include_directories(${LIB_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/${INCLUDE_DIR})
  target_link_libraries(${LIB_NAME} libprotobuf)
endfunction()

shk_proto(shk-store-proto include include/shk-store/shkstore.proto)

add_library(libshk-store
  src/dummy.cpp)
set_property(TARGET libshk-store APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET libshk-store PROPERTY CXX_STANDARD 14)
target_include_directories(libshk-store PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(libshk-store shk-store-proto)

add_executable(shk-store
  src/shk-store.cpp)
add_sanitizers(shk-store)
set_property(TARGET shk-store APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET shk-store PROPERTY CXX_STANDARD 14)
target_link_libraries(shk-store libshk-store rs-grpc)
set_target_properties(shk-store PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

add_executable(shk-store-test
  test/main.cpp)
set_property(TARGET shk-store-test PROPERTY CXX_STANDARD 14)
target_link_libraries(shk-store-test catch libshk-store)
target_include_directories(shk-store-test PRIVATE src)
set_property(TARGET shk-store-test APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME shk-store-unit COMMAND shk-store-test)
