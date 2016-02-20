add_subdirectory(kext)

add_executable(traceexec_test
  test/main.cpp)
set_property(TARGET traceexec_test PROPERTY CXX_STANDARD 11)
target_link_libraries(traceexec_test catch)
set_target_properties(traceexec_test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
