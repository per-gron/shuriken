# `rs` reference documentation

This document has API documentation for the rs Reactive Streams library.

Description
Type
See also
RxMarbles
Header

### Kinds

* <em><a name="kind_operator">Operator</a></em>: Function that returns a Publisher.
* <em><a name="kind_operator_builder">Operator Builder</a></em>: Function that returns an Operator.
* <em><a name="kind_operator_builder_builder">Operator Builder Builder</a></em>: Function that returns an Operator Builder.
* <em><a name="kind_core_library_api">Core Library API</a></em>: A function or type that is part of the core of rs, not an operator.


## `All(Predicate)`

**Defined in:** [`rs/all.h`](../include/rs/all.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `Average()`

**Defined in:** [`rs/average.h`](../include/rs/average.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `BuildPipe(Operator...)`

**Defined in:** [`rs/pipe.h`](../include/rs/pipe.h)

**Kind:** [Operator Builder Builder](#kind_operator_builder_builder)


## `Catch(Publisher)`

**Defined in:** [`rs/catch.h`](../include/rs/catch.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `Concat(Publisher...)`

**Defined in:** [`rs/concat.h`](../include/rs/concat.h)

**Kind:** [Operator](#kind_operator)


## `Count(Publisher...)`

**Defined in:** [`rs/count.h`](../include/rs/count.h)

**Kind:** [Operator](#kind_operator)


## `DefaultIfEmpty(Value...)`

**Defined in:** [`rs/default_if_empty.h`](../include/rs/default_if_empty.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `ElementAt(size_t)`

**Defined in:** [`rs/element_at.h`](../include/rs/element_at.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `ElementCount`

**Defined in:** [`rs/element_count.h`](../include/rs/element_count.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `Empty()`

**Defined in:** [`rs/empty.h`](../include/rs/empty.h)

**Kind:** [Operator](#kind_operator)


## `Filter(Predicate)`

**Defined in:** [`rs/filter.h`](../include/rs/filter.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `First()`

**Defined in:** [`rs/first.h`](../include/rs/first.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `First(Predicate)`

**Defined in:** [`rs/first.h`](../include/rs/first.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `FlatMap(Mapper)`

**Defined in:** [`rs/flat_map.h`](../include/rs/flat_map.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `From(Container)`

**Defined in:** [`rs/from.h`](../include/rs/from.h)

**Kind:** [Operator](#kind_operator)


## `IfEmpty(Publisher)`

**Defined in:** [`rs/if_empty.h`](../include/rs/if_empty.h)

**Kind:** [Operator Builder](#kind_operator_builder)


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


## `Last()`

**Defined in:** [`rs/last.h`](../include/rs/last.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `MakePublisher(Callback)`

**Defined in:** [`rs/publisher.h`](../include/rs/publisher.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `MakeSubscriber()`

**Defined in:** [`rs/subscriber.h`](../include/rs/subscriber.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `MakeSubscriber(OnNext, OnError, OnComplete)`

**Defined in:** [`rs/subscriber.h`](../include/rs/subscriber.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `MakeSubscriber(const std::shared_ptr<SubscriberType> &)`

**Defined in:** [`rs/subscriber.h`](../include/rs/subscriber.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `MakeSubscriber(const std::weak_ptr<SubscriberType> &)`

**Defined in:** [`rs/subscriber.h`](../include/rs/subscriber.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `MakeSubscription()`

**Defined in:** [`rs/subscription.h`](../include/rs/subscription.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `MakeSubscription(RequestCb, CancelCb)`

**Defined in:** [`rs/subscription.h`](../include/rs/subscription.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `MakeSubscription(const std::shared_ptr<SubscriptionType> &)`

**Defined in:** [`rs/subscription.h`](../include/rs/subscription.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `Map(Mapper)`

**Defined in:** [`rs/map.h`](../include/rs/map.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `Max()`

**Defined in:** [`rs/max.h`](../include/rs/max.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `Merge(Publisher...)`

**Defined in:** [`rs/merge.h`](../include/rs/merge.h)

**Kind:** [Operator](#kind_operator)


## `Min()`

**Defined in:** [`rs/min.h`](../include/rs/min.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `Never()`

**Defined in:** [`rs/never.h`](../include/rs/never.h)

**Kind:** [Operator](#kind_operator)


## `Pipe(Value, Operator...)`

**Defined in:** [`rs/pipe.h`](../include/rs/pipe.h)

**Kind:** [Operator](#kind_operator)


## `Publisher<>`

**Defined in:** [`rs/publisher.h`](../include/rs/publisher.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `PublisherBase`

**Defined in:** [`rs/publisher.h`](../include/rs/publisher.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `Range(Value, size_t)`

**Defined in:** [`rs/range.h`](../include/rs/range.h)

**Kind:** [Operator](#kind_operator)


## `Reduce(Accumulator, Reducer)`

**Defined in:** [`rs/reduce.h`](../include/rs/reduce.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `ReduceGet(MakeInitial, Reducer)`

**Defined in:** [`rs/reduce.h`](../include/rs/reduce.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `ReduceWithoutInitial(Reducer)`

**Defined in:** [`rs/reduce.h`](../include/rs/reduce.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `Repeat(Value, size_t)`

**Defined in:** [`rs/repeat.h`](../include/rs/repeat.h)

**Kind:** [Operator](#kind_operator)


## `Scan(Accumulator, Mapper)`

**Defined in:** [`rs/scan.h`](../include/rs/scan.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `Skip(size_t)`

**Defined in:** [`rs/skip.h`](../include/rs/skip.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `SkipWhile(Predicate)`

**Defined in:** [`rs/skip_while.h`](../include/rs/skip_while.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `Some(Predicate)`

**Defined in:** [`rs/some.h`](../include/rs/some.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `Splat(Callback)`

**Defined in:** [`rs/splat.h`](../include/rs/splat.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `Start(CreateValue)`

**Defined in:** [`rs/start.h`](../include/rs/start.h)

**Kind:** [Operator](#kind_operator)


## `StartWith(Value)`

**Defined in:** [`rs/start_with.h`](../include/rs/start_with.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `StartWithGet(MakeValue...)`

**Defined in:** [`rs/start_with.h`](../include/rs/start_with.h)

**Kind:** [Operator Builder](#kind_operator_builder)


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


## `Take(Count)`

**Defined in:** [`rs/take.h`](../include/rs/take.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `TakeWhile(Predicate)`

**Defined in:** [`rs/take_while.h`](../include/rs/take_while.h)

**Kind:** [Operator Builder](#kind_operator_builder)


## `Throw(Exception)`

**Defined in:** [`rs/throw.h`](../include/rs/throw.h)

**Kind:** [Operator](#kind_operator)


## `Throw(const std::exception_ptr &)`

**Defined in:** [`rs/throw.h`](../include/rs/throw.h)

**Kind:** [Operator](#kind_operator)


## `Zip(Publisher...)`

**Defined in:** [`rs/zip.h`](../include/rs/zip.h)

**Kind:** [Operator](#kind_operator)
