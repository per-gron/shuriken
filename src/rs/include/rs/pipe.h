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

#include <stdexcept>
#include <type_traits>

#include <rs/pipe.h>
#include <rs/reduce.h>

namespace shk {
namespace detail {

template <typename ...Operators>
class PipeOperator;

template <>
class PipeOperator<> {
 public:
  template <typename T>
  auto operator()(T &&t) const {
    return std::forward<T>(t);
  }
};

template <typename Operator, typename ...Operators>
class PipeOperator<Operator, Operators...> {
 public:
  template <typename OperatorT, typename ...OperatorTs>
  PipeOperator(OperatorT &&op, OperatorTs &&...ops)
      : op_(std::forward<OperatorT>(op)),
        inner_ops_(std::forward<OperatorTs>(ops)...) {}

  template <typename T>
  auto operator()(T &&t) {
    return inner_ops_(op_(std::forward<T>(t)));
  }

  template <typename T>
  auto operator()(T &&t) const {
    return inner_ops_(op_(std::forward<T>(t)));
  }

 private:
  Operator op_;
  PipeOperator<Operators...> inner_ops_;
};

}  // namespace detail

/**
 * Pipe is a helper function that makes it easy to pipe operators through each
 * other. It takes a bunch of operators and returns one that strings them
 * through each other, one by one.
 *
 * Pipe(a, b, c) is roughly equal to [](auto x) { return c(b(a(x))); }
 *
 * An example of usage, constructing an operator that takes a stream makes a
 * stream of the sum of squares in the inner stream:
 *
 *     Pipe(
 *         Map([](int x) { return x * x; }),
 *         Sum())
 *
 */
template <typename ...Operators>
auto Pipe(Operators &&...operators) {
  return detail::PipeOperator<typename std::decay<Operators>::type...>(
      std::forward<Operators>(operators)...);
}

}  // namespace shk
