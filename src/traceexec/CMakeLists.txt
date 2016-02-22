add_subdirectory(kext)

add_library(traceexec
  lib/version.cpp)
set_property(TARGET traceexec PROPERTY CXX_STANDARD 11)
set_target_properties(traceexec PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
target_link_libraries(traceexec util)
target_include_directories(traceexec PUBLIC include)
target_include_directories(traceexec PRIVATE kext)  # For traceexec_cmds.h

add_executable(traceexec_test
  test/main.cpp
  test/version_test.cpp)
set_property(TARGET traceexec_test PROPERTY CXX_STANDARD 11)
target_include_directories(traceexec_test PRIVATE lib)
target_link_libraries(traceexec_test traceexec catch)
set_target_properties(traceexec_test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
