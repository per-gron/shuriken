// Copyright 2017 Per Grön. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <memory>

#include <rs/concat.h>
#include <rs/concat_map.h>
#include <rs/end_with.h>
#include <rs/pipe.h>
#include <rs/start.h>

namespace shk {

/**
 * This is an rs operator that is a little bit like Reduce, but it is a little
 * bit more flexible: For each incoming value, it allows emitting the
 * accumulator value instead of only emitting a value at the end. The
 * accumulator is always emitted after the input stream ends.
 *
 * Like normal Reduce, the signature of the Reducer function is:
 *
 * Accumulator Reducer(Accumulator &&accum, Value &&value);
 *
 * The signature of ShouldEmit is:
 *
 * bool ShouldEmit(const Accumulator &accum, const Value &next_value);
 *
 * ShouldEmit is called before the call to Reducer. If it returns true, the
 * Accumulator value is emitted and reset to a default constructed value prior
 * to the subsequent call to Reducer.
 */
template <typename AccumulatorType, typename Reducer, typename ShouldEmit>
auto ReduceMultiple(
    AccumulatorType &&initial, Reducer &&reducer, ShouldEmit &&should_emit) {
  // Return an operator (it takes a Publisher and returns a Publisher)
  return [
      initial = std::forward<AccumulatorType>(initial),
      reducer = std::forward<Reducer>(reducer),
      should_emit = std::forward<ShouldEmit>(should_emit)](auto source) {
    using Accumulator = typename std::decay<AccumulatorType>::type;
    auto accum = std::make_shared<Accumulator>(initial);

    return Pipe(
        source,
        ConcatMap([accum, reducer, should_emit](auto &&value) mutable {
          Accumulator to_emit;

          using Value = typename std::decay<decltype(value)>::type;
          bool emit_now = should_emit(
              static_cast<const Accumulator &>(*accum),
              static_cast<const Value &>(value));
          if (emit_now) {
            to_emit = std::move(*accum);
            *accum = Accumulator();
          }

          *accum = reducer(
              std::move(*accum),
              std::forward<decltype(value)>(value));

          if (emit_now) {
            return AnyPublisher<Accumulator>(Just(std::move(to_emit)));
          } else {
            return AnyPublisher<Accumulator>(Empty());
          }
        }),
        EndWithGet([accum] { return std::move(*accum); }));
  };
}

}  // namespace shk
