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

set(PROTO_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/src")
set(GOOGLE_PROTO_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/vendor/googleapis")
set(BUILTIN_PROTO_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/vendor/grpc/third_party/protobuf/src")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cpp_proto")

function(shk_proto)
  set(OPTIONS GRPC GOOGLEAPI)
  set(ONE_VALUE_ARGS LIB_NAME PROTO_FILE)
  set(MULTI_VALUE_ARGS DEPENDENCIES)
  cmake_parse_arguments(shk_proto "${OPTIONS}" "${ONE_VALUE_ARGS}" "${MULTI_VALUE_ARGS}" ${ARGN})

  get_filename_component(PROTO_NAME ${shk_proto_PROTO_FILE} NAME_WE)
  get_filename_component(PROTO_DIR ${shk_proto_PROTO_FILE} DIRECTORY)

  if(${shk_proto_GOOGLEAPI})
    set(PROTO_PATH "${GOOGLE_PROTO_ROOT_DIR}/${shk_proto_PROTO_FILE}")
  else()
    set(PROTO_PATH "${PROTO_ROOT_DIR}/${shk_proto_PROTO_FILE}")
  endif()

  set(PROTO_PATHS --proto_path="${PROTO_ROOT_DIR}" --proto_path="${GOOGLE_PROTO_ROOT_DIR}" --proto_path="${BUILTIN_PROTO_ROOT_DIR}")

  set(PROTO_SRCS
    ${PROTO_OUT_DIR}/${shk_proto_LIB_NAME}/${PROTO_DIR}/${PROTO_NAME}.pb.cc
    ${PROTO_OUT_DIR}/${shk_proto_LIB_NAME}/${PROTO_DIR}/${PROTO_NAME}.pb.h)
  add_custom_command(
    OUTPUT ${PROTO_SRCS}
    COMMAND protoc --cpp_out="${PROTO_OUT_DIR}/${shk_proto_LIB_NAME}" ${PROTO_PATHS} "${PROTO_PATH}"
    DEPENDS "${PROTO_PATH}" protoc)

  if(${shk_proto_GRPC})
    set(PROTO_GRPC_SRCS
      ${PROTO_OUT_DIR}/${shk_proto_LIB_NAME}/${PROTO_DIR}/${PROTO_NAME}.grpc.pb.cc
      ${PROTO_OUT_DIR}/${shk_proto_LIB_NAME}/${PROTO_DIR}/${PROTO_NAME}.grpc.pb.h)
    set(PROTO_RSGRPC_SRCS
      ${PROTO_OUT_DIR}/${shk_proto_LIB_NAME}/${PROTO_DIR}/${PROTO_NAME}.rsgrpc.pb.cc
      ${PROTO_OUT_DIR}/${shk_proto_LIB_NAME}/${PROTO_DIR}/${PROTO_NAME}.rsgrpc.pb.h)
    set(PROTO_GRPC_DEP grpc++ rs-grpc)
    set(PROTO_SRCS ${PROTO_SRCS} ${PROTO_GRPC_SRCS} ${PROTO_RSGRPC_SRCS})

    add_custom_command(
      OUTPUT ${PROTO_GRPC_SRCS}
      COMMAND protoc --grpc_out="services_namespace=grpc:${PROTO_OUT_DIR}/${shk_proto_LIB_NAME}" --plugin=protoc-gen-grpc="$<TARGET_FILE:grpc_cpp_plugin>" ${PROTO_PATHS} "${PROTO_PATH}"
      DEPENDS "${PROTO_PATH}" protoc)

    add_custom_command(
      OUTPUT ${PROTO_RSGRPC_SRCS}
      COMMAND protoc --grpc_out="grpc_services_namespace=grpc:${PROTO_OUT_DIR}/${shk_proto_LIB_NAME}" --plugin=protoc-gen-grpc="$<TARGET_FILE:rs_grpc_plugin>" ${PROTO_PATHS} "${PROTO_PATH}"
      DEPENDS "${PROTO_PATH}" protoc)
  endif()

  add_library(${shk_proto_LIB_NAME} ${PROTO_SRCS})
  set_property(TARGET ${shk_proto_LIB_NAME} APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
  set_property(TARGET ${shk_proto_LIB_NAME} PROPERTY CXX_STANDARD 14)
  target_include_directories(${shk_proto_LIB_NAME} PUBLIC ${PROTO_OUT_DIR}/${shk_proto_LIB_NAME})
  target_link_libraries(${shk_proto_LIB_NAME} libprotobuf ${PROTO_GRPC_DEP} ${shk_proto_DEPENDENCIES})
endfunction()

function(shk_cpp_target NAME)
  set_property(TARGET ${NAME} APPEND_STRING PROPERTY COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
  set_property(TARGET ${NAME} PROPERTY CXX_STANDARD 14)
  add_sanitizers(${NAME})
endfunction()

function(shk_library)
  set(OPTIONS)
  set(ONE_VALUE_ARGS NAME)
  set(MULTI_VALUE_ARGS SOURCES DEPENDENCIES PUBLIC_INCLUDES)
  cmake_parse_arguments(shk_library "${OPTIONS}" "${ONE_VALUE_ARGS}" "${MULTI_VALUE_ARGS}" ${ARGN})

  add_library(${shk_library_NAME}
    ${shk_library_SOURCES})
  shk_cpp_target(${shk_library_NAME})
  target_link_libraries(${shk_library_NAME} ${shk_library_DEPENDENCIES})
  target_include_directories(${shk_library_NAME} PUBLIC ${shk_library_PUBLIC_INCLUDES})
endfunction()

function(shk_executable)
  set(OPTIONS USER_FACING)
  set(ONE_VALUE_ARGS NAME)
  set(MULTI_VALUE_ARGS SOURCES DEPENDENCIES PRIVATE_INCLUDES)
  cmake_parse_arguments(shk_executable "${OPTIONS}" "${ONE_VALUE_ARGS}" "${MULTI_VALUE_ARGS}" ${ARGN})

  add_executable(${shk_executable_NAME}
    ${shk_executable_SOURCES})
  shk_cpp_target(${shk_executable_NAME})
  target_link_libraries(${shk_executable_NAME} ${shk_executable_DEPENDENCIES})
  target_include_directories(${shk_executable_NAME} PRIVATE ${shk_executable_PRIVATE_INCLUDES})

  if (${shk_executable_USER_FACING})
    set_target_properties(${shk_executable_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
  endif()
endfunction()

function(shk_test)
  set(OPTIONS)
  set(ONE_VALUE_ARGS NAME)
  set(MULTI_VALUE_ARGS)
  cmake_parse_arguments(shk_test "${OPTIONS}" "${ONE_VALUE_ARGS}" "${MULTI_VALUE_ARGS}" ${ARGN})
  shk_executable(${ARGV})
  add_test(NAME ${shk_test_NAME} COMMAND ${shk_test_NAME})
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
