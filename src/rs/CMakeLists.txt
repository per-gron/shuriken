add_library(rs
  include/rs/count.h
  include/rs/empty.h
  include/rs/error.h
  include/rs/iterate.h
  include/rs/just.h
  include/rs/map.h
  include/rs/publisher.h
  include/rs/reduce.h
  include/rs/subscriber.h
  include/rs/subscription.h
  include/rs/sum.h
  src/subscription.cpp)
target_include_directories(rs PUBLIC include)
set_target_properties(rs PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET rs PROPERTY CXX_STANDARD 14)

add_executable(rs-test
  test/count_test.cpp
  test/empty_test.cpp
  test/error_test.cpp
  test/iterate_test.cpp
  test/just_test.cpp
  test/main.cpp
  test/map_test.cpp
  test/publisher_test.cpp
  test/reduce_test.cpp
  test/sum_test.cpp
  test/test_util.h)
set_property(TARGET rs-test PROPERTY CXX_STANDARD 14)
target_link_libraries(rs-test rs catch)
set_target_properties(rs-test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME rs-unit COMMAND rs-test)
