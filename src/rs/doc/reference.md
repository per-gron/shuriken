# `rs` reference documentation

This document has API documentation for the rs Reactive Streams library.

Description
See also


## `All(Predicate)`

**Defined in:** [`rs/all.h`](../include/rs/all.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `(a -> bool) -> (Publisher[a] -> Publisher[bool])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#every), [ReactiveX](http://reactivex.io/documentation/operators/all.html)


## `Average()`

**Defined in:** [`rs/average.h`](../include/rs/average.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `void -> (Publisher[a] -> Publisher[ResultType])`, where `ResultType` is `double` by default and configurable by a template parameter to `Average()`.

**External documentation:** [RxMarbles](http://rxmarbles.com/#average), [ReactiveX](http://reactivex.io/documentation/operators/average.html)


## `BuildPipe(Operator...)`

**Defined in:** [`rs/pipe.h`](../include/rs/pipe.h)

**Kind:** [Operator Builder Builder](#kind_operator_builder_builder)

**[Type](#types):** `(Publisher[a] -> Publisher[b]), (Publisher[b] -> Publisher[c]), ... -> (Publisher[a] -> Publisher[c])`


## `Catch(Publisher)`

**Defined in:** [`rs/catch.h`](../include/rs/catch.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `(std::exception_ptr&& -> Publisher[b]) -> (Publisher[a] -> Publisher[a, b])`

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/catch.html)


## `Concat(Publisher...)`

**Defined in:** [`rs/concat.h`](../include/rs/concat.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `Publisher[a], Publisher[b], ... -> Publisher[a, b, ...]`

**External documentation:** [RxMarbles](http://rxmarbles.com/#concat), [ReactiveX](http://reactivex.io/documentation/operators/concat.html)


## `Count(Publisher...)`

**Defined in:** [`rs/count.h`](../include/rs/count.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `void -> (Publisher[a] -> Publisher[CountType])`, where `CountType` is `int` by default and configurable by a template parameter to `Count()`.

**External documentation:** [RxMarbles](http://rxmarbles.com/#count), [ReactiveX](http://reactivex.io/documentation/operators/count.html)


## `DefaultIfEmpty(Value...)`

**Defined in:** [`rs/default_if_empty.h`](../include/rs/default_if_empty.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `a, b, ... -> (Publisher[c] -> Publisher[a, b, c, ...])`

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/defaultifempty.html)


## `ElementAt(size_t)`

**Defined in:** [`rs/element_at.h`](../include/rs/element_at.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `size_t -> (Publisher[a] -> Publisher[a])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#elementAt), [ReactiveX](http://reactivex.io/documentation/operators/elementat.html)


## `ElementCount`

**Defined in:** [`rs/element_count.h`](../include/rs/element_count.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `Empty()`

**Defined in:** [`rs/empty.h`](../include/rs/empty.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `void -> Publisher[]`

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/empty-never-throw.html)


## `Filter(Predicate)`

**Defined in:** [`rs/filter.h`](../include/rs/filter.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `(a -> bool) -> (Publisher[a] -> Publisher[a])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#filter), [ReactiveX](http://reactivex.io/documentation/operators/filter.html)


## `First()`

**Defined in:** [`rs/first.h`](../include/rs/first.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `void -> (Publisher[a] -> Publisher[a])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#first), [ReactiveX](http://reactivex.io/documentation/operators/first.html)


## `First(Predicate)`

**Defined in:** [`rs/first.h`](../include/rs/first.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `(a -> bool) -> (Publisher[a] -> Publisher[a])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#first), [ReactiveX](http://reactivex.io/documentation/operators/first.html)


## `FlatMap(Mapper)`

**Defined in:** [`rs/flat_map.h`](../include/rs/flat_map.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `(a -> Publisher[b]) -> (Publisher[a] -> Publisher[b])`

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/flatmap.html)


## `From(Container)`

**Defined in:** [`rs/from.h`](../include/rs/from.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `Container[a] -> Publisher[a]`, where `Container[a]` is an STL-style container, for example `std::vector<a>`.

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/from.html)


## `IfEmpty(Publisher)`

**Defined in:** [`rs/if_empty.h`](../include/rs/if_empty.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `Publisher[a] -> (Publisher[b] -> Publisher[a, b])`


## `IsPublisher`

**Defined in:** [`rs/publisher.h`](../include/rs/publisher.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `IsRvalue`

**Defined in:** [`rs/subscriber.h`](../include/rs/subscriber.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `IsSubscriber`

**Defined in:** [`rs/subscriber.h`](../include/rs/subscriber.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `IsSubscription`

**Defined in:** [`rs/subscription.h`](../include/rs/subscription.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `Just(Value...)`

**Defined in:** [`rs/just.h`](../include/rs/just.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `a, b, ... -> Publisher[a, b, ...]`

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/just.html)


## `Last()`

**Defined in:** [`rs/last.h`](../include/rs/last.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `void -> (Publisher[a] -> Publisher[a])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#last), [ReactiveX](http://reactivex.io/documentation/operators/last.html)


## `MakePublisher(Callback)`

**Defined in:** [`rs/publisher.h`](../include/rs/publisher.h)

**Kind:** [Core Library API](#kind_core_library_api)

**[Type](#types):** `(Subscriber[a]&& -> Subscription[]) -> Publisher[a]`


## `MakeSubscriber()`

**Defined in:** [`rs/subscriber.h`](../include/rs/subscriber.h)

**Kind:** [Core Library API](#kind_core_library_api)

**[Type](#types):** `void -> Subscriber[a]`


## `MakeSubscriber(OnNext, OnError, OnComplete)`

**Defined in:** [`rs/subscriber.h`](../include/rs/subscriber.h)

**Kind:** [Core Library API](#kind_core_library_api)

**[Type](#types):** `(a -> void), (std::exception_ptr&& -> void), (void -> void) -> Subscriber[a]`


## `MakeSubscriber(const std::shared_ptr<SubscriberType> &)`

**Defined in:** [`rs/subscriber.h`](../include/rs/subscriber.h)

**Kind:** [Core Library API](#kind_core_library_api)

**[Type](#types):** `std::shared_ptr<Subscriber[a]> -> Subscriber[a]`


## `MakeSubscriber(const std::weak_ptr<SubscriberType> &)`

**Defined in:** [`rs/subscriber.h`](../include/rs/subscriber.h)

**Kind:** [Core Library API](#kind_core_library_api)

**[Type](#types):** `std::weak_ptr<Subscriber[a]> -> Subscriber[a]`


## `MakeSubscription()`

**Defined in:** [`rs/subscription.h`](../include/rs/subscription.h)

**Kind:** [Core Library API](#kind_core_library_api)

**[Type](#types):** `void -> Subscription[]`


## `MakeSubscription(RequestCb, CancelCb)`

**Defined in:** [`rs/subscription.h`](../include/rs/subscription.h)

**Kind:** [Core Library API](#kind_core_library_api)

**[Type](#types):** `(ElementCount -> void), (void -> void) -> Subscription[]`


## `MakeSubscription(const std::shared_ptr<SubscriptionType> &)`

**Defined in:** [`rs/subscription.h`](../include/rs/subscription.h)

**Kind:** [Core Library API](#kind_core_library_api)

**[Type](#types):** `std::shared_ptr<Subscription[]> -> Subscription[]`


## `Map(Mapper)`

**Defined in:** [`rs/map.h`](../include/rs/map.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `(a -> b) -> (Publisher[a] -> Publisher[b])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#map), [ReactiveX](http://reactivex.io/documentation/operators/map.html)


## `Max()`

**Defined in:** [`rs/max.h`](../include/rs/max.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `void -> (Publisher[a] -> Publisher[a])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#max), [ReactiveX](http://reactivex.io/documentation/operators/max.html)


## `Merge(Publisher...)`

**Defined in:** [`rs/merge.h`](../include/rs/merge.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `Publisher[a], Publisher[b], ... -> Publisher[a, b, ...]`

**External documentation:** [RxMarbles](http://rxmarbles.com/#merge), [ReactiveX](http://reactivex.io/documentation/operators/merge.html)


## `Min()`

**Defined in:** [`rs/min.h`](../include/rs/min.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `void -> (Publisher[a] -> Publisher[a])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#min), [ReactiveX](http://reactivex.io/documentation/operators/min.html)


## `Never()`

**Defined in:** [`rs/never.h`](../include/rs/never.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `void -> Publisher[]`

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/empty-never-throw.html)


## `Pipe(Value, Operator...)`

**Defined in:** [`rs/pipe.h`](../include/rs/pipe.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `a, (Publisher[a] -> Publisher[b]), (Publisher[b] -> Publisher[c]), ... -> Publisher[c]`


## `Publisher<>`

**Defined in:** [`rs/publisher.h`](../include/rs/publisher.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `PublisherBase`

**Defined in:** [`rs/publisher.h`](../include/rs/publisher.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `Range(Value, size_t)`

**Defined in:** [`rs/range.h`](../include/rs/range.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `a, size_t -> Publisher[Numberic]`, where `a` is any numeric type.

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/range.html)


## `Reduce(Accumulator, Reducer)`

**Defined in:** [`rs/reduce.h`](../include/rs/reduce.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `a, (a, b -> a) -> (Publisher[b] -> Publisher[a])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#reduce), [ReactiveX](http://reactivex.io/documentation/operators/reduce.html)


## `ReduceGet(MakeInitial, Reducer)`

**Defined in:** [`rs/reduce.h`](../include/rs/reduce.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `(void -> a), (a, b -> a) -> (Publisher[b] -> Publisher[a])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#reduce), [ReactiveX](http://reactivex.io/documentation/operators/reduce.html)


## `ReduceWithoutInitial(Reducer)`

**Defined in:** [`rs/reduce.h`](../include/rs/reduce.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `(a, a -> a) -> (Publisher[a] -> Publisher[a])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#reduce), [ReactiveX](http://reactivex.io/documentation/operators/reduce.html)


## `Repeat(Value, size_t)`

**Defined in:** [`rs/repeat.h`](../include/rs/repeat.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `a, size_t -> Publisher[a]`

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/repeat.html)


## `Scan(Accumulator, Mapper)`

**Defined in:** [`rs/scan.h`](../include/rs/scan.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `a, (b -> a) -> (Publisher[b] -> Publisher[a])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#scan), [ReactiveX](http://reactivex.io/documentation/operators/scan.html)


## `Skip(size_t)`

**Defined in:** [`rs/skip.h`](../include/rs/skip.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `size_t -> (Publisher[a] -> Publisher[a])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#skip), [ReactiveX](http://reactivex.io/documentation/operators/skip.html)


## `SkipWhile(Predicate)`

**Defined in:** [`rs/skip_while.h`](../include/rs/skip_while.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `(a -> bool) -> (Publisher[a] -> Publisher[a])`

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/skipwhile.html)


## `Some(Predicate)`

**Defined in:** [`rs/some.h`](../include/rs/some.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `(a -> bool) -> (Publisher[a] -> Publisher[bool])`

**External documentation:** [RxMarbles](http://rxmarbles.com/#some)


## `Splat(Callback)`

**Defined in:** [`rs/splat.h`](../include/rs/splat.h)

**Kind:** [Core Library API](#kind_core_library_api)

**[Type](#types):** `(a, b, ... -> c) -> (std::tuple<a, b, ...>&& -> c)`


## `Start(CreateValue)`

**Defined in:** [`rs/start.h`](../include/rs/start.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `(void -> a) -> Publisher[a]`

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/start.html)


## `StartWith(Value)`

**Defined in:** [`rs/start_with.h`](../include/rs/start_with.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `a, b, ... -> (Publisher[c] -> Publisher[a, b, ..., c]`

**External documentation:** [RxMarbles](http://rxmarbles.com/#startWith), [ReactiveX](http://reactivex.io/documentation/operators/startwith.html)


## `StartWithGet(MakeValue...)`

**Defined in:** [`rs/start_with.h`](../include/rs/start_with.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `(void -> a), (void -> b), ... -> (Publisher[c] -> Publisher[a, b, ..., c]`

**External documentation:** [RxMarbles](http://rxmarbles.com/#startWith), [ReactiveX](http://reactivex.io/documentation/operators/startwith.html)


## `Subscriber<>`

**Defined in:** [`rs/subscriber.h`](../include/rs/subscriber.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `SubscriberBase`

**Defined in:** [`rs/subscriber.h`](../include/rs/subscriber.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `Subscription`

**Defined in:** [`rs/subscription.h`](../include/rs/subscription.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `SubscriptionBase`

**Defined in:** [`rs/subscription.h`](../include/rs/subscription.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `Sum()`

**Defined in:** [`rs/sum.h`](../include/rs/sum.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `void -> (Publisher[a] -> Publisher[SumType])`, where `SumType` is `int` by default and configurable by a template parameter to `Sum()`.

**External documentation:** [RxMarbles](http://rxmarbles.com/#sum), [ReactiveX](http://reactivex.io/documentation/operators/sum.html)


## `Take(Count)`

**Defined in:** [`rs/take.h`](../include/rs/take.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `CountType -> (Publisher[a] -> Publisher[a])`, where `CountType` is a numeric type, for example `int`, `size_t` or `ElementCount`.

**External documentation:** [RxMarbles](http://rxmarbles.com/#take), [ReactiveX](http://reactivex.io/documentation/operators/take.html)


## `TakeWhile(Predicate)`

**Defined in:** [`rs/take_while.h`](../include/rs/take_while.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**[Type](#types):** `(a -> bool) -> (Publisher[a] -> Publisher[a])`

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/takewhile.html)


## `Throw(Exception)`

**Defined in:** [`rs/throw.h`](../include/rs/throw.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `ExceptionType -> Publisher[]`, where `ExceptionType` is anything that can be thrown as an exception in C++ except an `std::exception_ptr`.

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/empty-never-throw.html)


## `Throw(const std::exception_ptr &)`

**Defined in:** [`rs/throw.h`](../include/rs/throw.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `std::exception_ptr -> Publisher[]`

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/empty-never-throw.html)


## `Zip(Publisher...)`

**Defined in:** [`rs/zip.h`](../include/rs/zip.h)

**Kind:** [Operator](#kind_operator)

**[Type](#types):** `Publisher[a], Publisher[b], ... -> Publisher[std::tuple<a, b, ...>]`

**External documentation:** [RxMarbles](http://rxmarbles.com/#zip), [ReactiveX](http://reactivex.io/documentation/operators/zip.html)


## Legend

### Kinds

* <em><a name="kind_operator">Operator</a></em>: Function that returns a Publisher.
* <em><a name="kind_operator_builder">Operator Builder</a></em>: Function that returns an Operator.
* <em><a name="kind_operator_builder_builder">Operator Builder Builder</a></em>: Function that returns an Operator Builder.
* <em><a name="kind_core_library_api">Core Library API</a></em>: A function or type that is part of the core of rs, not an operator.


### Types

Each operator described here has a section that describes its type. Because C++ does not have a syntax for concepts, these descrpitions have a made-up syntax:

* `Publisher[x]` is a C++ type that fulfills the requirements of the Publisher concept that publishes elements of type `x`.
* `Publisher[x, y]` is a Publisher that can emit elements of type `x` and `y`.
* `Subscriber[x]` is a C++ type that fulfills the requirements of the Subscriber concept that publishes elements of type `x`.
* `Subscription[]` is a C++ type that fulfills the requirements of the Subscription concept.
* `x -> y` is a function that takes `x` as a parameter and returns `y`.
* `x, y -> z` is a function with two parameters of type `x` and `y` and returns `z`.
* `bool`, `size_t` and `std::exception_ptr&&` refer to the C++ types with the same name.
* `void` is the C++ `void` type. It is also used here to denote functions that take no parameters: for example, `void -> bool` a function that takes no parameters and returns a `bool`.
* Letters such as `a`, `b` and `c` can represent any type. If a letter occurs more than once in a type declaration, all occurences refer to the same type.
