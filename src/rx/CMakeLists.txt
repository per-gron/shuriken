add_library(rx
  include/rx/count.h
  include/rx/empty.h
  include/rx/error.h
  include/rx/iterate.h
  include/rx/just.h
  include/rx/publisher.h
  include/rx/reduce.h
  include/rx/subscriber.h
  include/rx/subscription.h
  src/subscription.cpp)
target_include_directories(rx PUBLIC include)
set_target_properties(rx PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET rx PROPERTY CXX_STANDARD 14)

add_executable(rx-test
  test/count_test.cpp
  test/empty_test.cpp
  test/error_test.cpp
  test/iterate_test.cpp
  test/just_test.cpp
  test/main.cpp
  test/publisher_test.cpp
  test/reduce_test.cpp)
set_property(TARGET rx-test PROPERTY CXX_STANDARD 14)
target_link_libraries(rx-test rx catch)
set_target_properties(rx-test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME rx-unit COMMAND rx-test)
