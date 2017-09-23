cmake_minimum_required(VERSION 3.2.1)
project(shk)

enable_testing()

set(SHK_COMPILER_FLAGS "-Wall -Wimplicit-fallthrough -Werror")

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(CMAKE_C_ARCHIVE_CREATE   "<CMAKE_AR> Scr <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> Scr <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_C_ARCHIVE_FINISH   "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
  set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
endif()

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

add_subdirectory(vendor)
add_subdirectory(src/benchmark)
add_subdirectory(src/rs)
add_subdirectory(src/rs-grpc)
add_subdirectory(src/shk)
add_subdirectory(src/shk-cache)
add_subdirectory(src/shk-index)
add_subdirectory(src/shk-store)
add_subdirectory(src/shk-trace)
add_subdirectory(src/shk-util)
