set(BLAKE2_DIR ../blake2/sse)

add_library(blake2
  ${BLAKE2_DIR}/blake2b.c
  ${BLAKE2_DIR}/blake2bp.c
  ${BLAKE2_DIR}/blake2s.c
  ${BLAKE2_DIR}/blake2sp.c)

target_include_directories(blake2 PUBLIC ${BLAKE2_DIR})
set_target_properties(blake2 PROPERTIES COMPILE_FLAGS "-O3 -pedantic")
