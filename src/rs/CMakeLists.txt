add_library(rs
  include/rs/all.h
  include/rs/average.h
  include/rs/concat.h
  include/rs/count.h
  include/rs/default_if_empty.h
  include/rs/element_at.h
  include/rs/element_count.h
  include/rs/empty.h
  include/rs/filter.h
  include/rs/first.h
  include/rs/flat_map.h
  include/rs/from.h
  include/rs/if_empty.h
  include/rs/is_empty.h
  include/rs/just.h
  include/rs/last.h
  include/rs/map.h
  include/rs/max.h
  include/rs/merge.h
  include/rs/min.h
  include/rs/never.h
  include/rs/pipe.h
  include/rs/publisher.h
  include/rs/range.h
  include/rs/reduce.h
  include/rs/repeat.h
  include/rs/scan.h
  include/rs/some.h
  include/rs/start.h
  include/rs/start_with.h
  include/rs/subscriber.h
  include/rs/subscription.h
  include/rs/sum.h
  include/rs/take.h
  include/rs/take_while.h
  include/rs/throw.h
  src/subscription.cpp)
target_include_directories(rs PUBLIC include)
set_target_properties(rs PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})
set_property(TARGET rs PROPERTY CXX_STANDARD 14)

add_executable(rs-test
  test/all_test.cpp
  test/average_test.cpp
  test/backpressure_violator.h
  test/concat_test.cpp
  test/count_test.cpp
  test/default_if_empty_test.cpp
  test/element_at_test.cpp
  test/element_count_test.cpp
  test/empty_test.cpp
  test/filter_test.cpp
  test/first_test.cpp
  test/flat_map_test.cpp
  test/from_test.cpp
  test/if_empty_test.cpp
  test/is_empty_test.cpp
  test/just_test.cpp
  test/last_test.cpp
  test/main.cpp
  test/map_test.cpp
  test/max_test.cpp
  test/merge_test.cpp
  test/min_test.cpp
  test/never_test.cpp
  test/pipe_test.cpp
  test/publisher_test.cpp
  test/range_test.cpp
  test/reduce_test.cpp
  test/repeat_test.cpp
  test/scan_test.cpp
  test/some_test.cpp
  test/start_test.cpp
  test/start_with_test.cpp
  test/take_test.cpp
  test/take_while_test.cpp
  test/sum_test.cpp
  test/test_util.h
  test/throw_test.cpp)
set_property(TARGET rs-test PROPERTY CXX_STANDARD 14)
target_link_libraries(rs-test rs catch)
set_target_properties(rs-test PROPERTIES COMPILE_FLAGS ${SHK_COMPILER_FLAGS})

add_test(NAME rs-unit COMMAND rs-test)
