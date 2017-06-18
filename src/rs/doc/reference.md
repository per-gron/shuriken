# `rs` reference documentation

This document has API documentation for the rs Reactive Streams library.

Description
Type
See also

### Kinds

* <em><a name="kind_operator">Operator</a></em>: Function that returns a Publisher.
* <em><a name="kind_operator_builder">Operator Builder</a></em>: Function that returns an Operator.
* <em><a name="kind_operator_builder_builder">Operator Builder Builder</a></em>: Function that returns an Operator Builder.
* <em><a name="kind_core_library_api">Core Library API</a></em>: A function or type that is part of the core of rs, not an operator.


## `All(Predicate)`

**Defined in:** [`rs/all.h`](../include/rs/all.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#every), [ReactiveX](http://reactivex.io/documentation/operators/all.html)


## `Average()`

**Defined in:** [`rs/average.h`](../include/rs/average.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#average), [ReactiveX](http://reactivex.io/documentation/operators/average.html)


## `BuildPipe(Operator...)`

**Defined in:** [`rs/pipe.h`](../include/rs/pipe.h)

**Kind:** [Operator Builder Builder](#kind_operator_builder_builder)


## `Catch(Publisher)`

**Defined in:** [`rs/catch.h`](../include/rs/catch.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/catch.html)


## `Concat(Publisher...)`

**Defined in:** [`rs/concat.h`](../include/rs/concat.h)

**Kind:** [Operator](#kind_operator)

**External documentation:** [RxMarbles](http://rxmarbles.com/#concat), [ReactiveX](http://reactivex.io/documentation/operators/concat.html)


## `Count(Publisher...)`

**Defined in:** [`rs/count.h`](../include/rs/count.h)

**Kind:** [Operator](#kind_operator)

**External documentation:** [RxMarbles](http://rxmarbles.com/#count), [ReactiveX](http://reactivex.io/documentation/operators/count.html)


## `DefaultIfEmpty(Value...)`

**Defined in:** [`rs/default_if_empty.h`](../include/rs/default_if_empty.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/defaultifempty.html)


## `ElementAt(size_t)`

**Defined in:** [`rs/element_at.h`](../include/rs/element_at.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#elementAt), [ReactiveX](http://reactivex.io/documentation/operators/elementat.html)


## `ElementCount`

**Defined in:** [`rs/element_count.h`](../include/rs/element_count.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `Empty()`

**Defined in:** [`rs/empty.h`](../include/rs/empty.h)

**Kind:** [Operator](#kind_operator)

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/empty-never-throw.html)


## `Filter(Predicate)`

**Defined in:** [`rs/filter.h`](../include/rs/filter.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#filter), [ReactiveX](http://reactivex.io/documentation/operators/filter.html)


## `First()`

**Defined in:** [`rs/first.h`](../include/rs/first.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#first), [ReactiveX](http://reactivex.io/documentation/operators/first.html)


## `First(Predicate)`

**Defined in:** [`rs/first.h`](../include/rs/first.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#first), [ReactiveX](http://reactivex.io/documentation/operators/first.html)


## `FlatMap(Mapper)`

**Defined in:** [`rs/flat_map.h`](../include/rs/flat_map.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/flatmap.html)


## `From(Container)`

**Defined in:** [`rs/from.h`](../include/rs/from.h)

**Kind:** [Operator](#kind_operator)

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/from.html)


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

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/just.html)


## `Last()`

**Defined in:** [`rs/last.h`](../include/rs/last.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#last), [ReactiveX](http://reactivex.io/documentation/operators/last.html)


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

**External documentation:** [RxMarbles](http://rxmarbles.com/#map), [ReactiveX](http://reactivex.io/documentation/operators/map.html)


## `Max()`

**Defined in:** [`rs/max.h`](../include/rs/max.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#max), [ReactiveX](http://reactivex.io/documentation/operators/max.html)


## `Merge(Publisher...)`

**Defined in:** [`rs/merge.h`](../include/rs/merge.h)

**Kind:** [Operator](#kind_operator)

**External documentation:** [RxMarbles](http://rxmarbles.com/#merge), [ReactiveX](http://reactivex.io/documentation/operators/merge.html)


## `Min()`

**Defined in:** [`rs/min.h`](../include/rs/min.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#min), [ReactiveX](http://reactivex.io/documentation/operators/min.html)


## `Never()`

**Defined in:** [`rs/never.h`](../include/rs/never.h)

**Kind:** [Operator](#kind_operator)

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/empty-never-throw.html)


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

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/range.html)


## `Reduce(Accumulator, Reducer)`

**Defined in:** [`rs/reduce.h`](../include/rs/reduce.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#reduce), [ReactiveX](http://reactivex.io/documentation/operators/reduce.html)


## `ReduceGet(MakeInitial, Reducer)`

**Defined in:** [`rs/reduce.h`](../include/rs/reduce.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#reduce), [ReactiveX](http://reactivex.io/documentation/operators/reduce.html)


## `ReduceWithoutInitial(Reducer)`

**Defined in:** [`rs/reduce.h`](../include/rs/reduce.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#reduce), [ReactiveX](http://reactivex.io/documentation/operators/reduce.html)


## `Repeat(Value, size_t)`

**Defined in:** [`rs/repeat.h`](../include/rs/repeat.h)

**Kind:** [Operator](#kind_operator)

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/repeat.html)


## `Scan(Accumulator, Mapper)`

**Defined in:** [`rs/scan.h`](../include/rs/scan.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#scan), [ReactiveX](http://reactivex.io/documentation/operators/scan.html)


## `Skip(size_t)`

**Defined in:** [`rs/skip.h`](../include/rs/skip.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#skip), [ReactiveX](http://reactivex.io/documentation/operators/skip.html)


## `SkipWhile(Predicate)`

**Defined in:** [`rs/skip_while.h`](../include/rs/skip_while.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/skipwhile.html)


## `Some(Predicate)`

**Defined in:** [`rs/some.h`](../include/rs/some.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#some)


## `Splat(Callback)`

**Defined in:** [`rs/splat.h`](../include/rs/splat.h)

**Kind:** [Core Library API](#kind_core_library_api)


## `Start(CreateValue)`

**Defined in:** [`rs/start.h`](../include/rs/start.h)

**Kind:** [Operator](#kind_operator)

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/start.html)


## `StartWith(Value)`

**Defined in:** [`rs/start_with.h`](../include/rs/start_with.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#startWith), [ReactiveX](http://reactivex.io/documentation/operators/startwith.html)


## `StartWithGet(MakeValue...)`

**Defined in:** [`rs/start_with.h`](../include/rs/start_with.h)

**Kind:** [Operator Builder](#kind_operator_builder)

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

**External documentation:** [RxMarbles](http://rxmarbles.com/#sum), [ReactiveX](http://reactivex.io/documentation/operators/sum.html)


## `Take(Count)`

**Defined in:** [`rs/take.h`](../include/rs/take.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [RxMarbles](http://rxmarbles.com/#take), [ReactiveX](http://reactivex.io/documentation/operators/take.html)


## `TakeWhile(Predicate)`

**Defined in:** [`rs/take_while.h`](../include/rs/take_while.h)

**Kind:** [Operator Builder](#kind_operator_builder)

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/takewhile.html)


## `Throw(Exception)`

**Defined in:** [`rs/throw.h`](../include/rs/throw.h)

**Kind:** [Operator](#kind_operator)

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/empty-never-throw.html)


## `Throw(const std::exception_ptr &)`

**Defined in:** [`rs/throw.h`](../include/rs/throw.h)

**Kind:** [Operator](#kind_operator)

**External documentation:** [ReactiveX](http://reactivex.io/documentation/operators/empty-never-throw.html)


## `Zip(Publisher...)`

**Defined in:** [`rs/zip.h`](../include/rs/zip.h)

**Kind:** [Operator](#kind_operator)

**External documentation:** [RxMarbles](http://rxmarbles.com/#zip), [ReactiveX](http://reactivex.io/documentation/operators/zip.html)
