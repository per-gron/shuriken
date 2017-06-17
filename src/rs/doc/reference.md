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
* <em><a name="kind_operator_builder_builder">Operator Builder Builder</a></em>: Function that returns an Operator Builder
* <em><a name="kind_core_library_api">Core Library API</a></em>: A function or type that is part of the core of rs, not an operator.


## `All(Predicate)`

**Defined in:** `rs/all.h`

**Kind:** Operator Builder


## `Average()`

**Defined in:** `rs/average.h`

**Kind:** Operator Builder


## `BuildPipe(Operator...)`

**Defined in:** `rs/pipe.h`

**Kind:** Operator Builder Builder


## `Catch(Publisher)`

**Defined in:** `rs/catch.h`

**Kind:** Operator Builder


## `Concat(Publisher...)`

**Defined in:** `rs/concat.h`

**Kind:** Operator


## `Count(Publisher...)`

**Defined in:** `rs/count.h`

**Kind:** Operator


## `DefaultIfEmpty(Value...)`

**Defined in:** `rs/default_if_empty.h`

**Kind:** Operator Builder


## `ElementAt(size_t)`

**Defined in:** `rs/element_at.h`

**Kind:** Operator Builder


## `ElementCount`

**Defined in:** `rs/element_count.h`

**Kind:** Core Library API


## `Empty()`

**Defined in:** `rs/empty.h`

**Kind:** Operator


## `Filter(Predicate)`

**Defined in:** `rs/filter.h`

**Kind:** Operator Builder


## `First()`

**Defined in:** `rs/first.h`

**Kind:** Operator Builder


## `First(Predicate)`

**Defined in:** `rs/first.h`

**Kind:** Operator Builder


## `FlatMap(Mapper)`

**Defined in:** `rs/flat_map.h`

**Kind:** Operator Builder


## `From(Container)`

**Defined in:** `rs/from.h`

**Kind:** Operator


## `IfEmpty(Publisher)`

**Defined in:** `rs/if_empty.h`

**Kind:** Operator Builder


## `IsPublisher`

**Defined in:** `rs/publisher.h`

**Kind:** Core Library API


## `IsRvalue`

**Defined in:** `rs/subscriber.h`

**Kind:** Core Library API


## `IsSubscriber`

**Defined in:** `rs/subscriber.h`

**Kind:** Core Library API


## `IsSubscription`

**Defined in:** `rs/subscription.h`

**Kind:** Core Library API


## `Just(Value...)`

**Defined in:** `rs/just.h`

**Kind:** Operator


## `Last()`

**Defined in:** `rs/last.h`

**Kind:** Operator Builder


## `MakePublisher(Callback)`

**Defined in:** `rs/publisher.h`

**Kind:** Core Library API


## `MakeSubscriber()`

**Defined in:** `rs/subscriber.h`

**Kind:** Core Library API


## `MakeSubscriber(OnNext, OnError, OnComplete)`

**Defined in:** `rs/subscriber.h`

**Kind:** Core Library API


## `MakeSubscriber(const std::shared_ptr<SubscriberType> &)`

**Defined in:** `rs/subscriber.h`

**Kind:** Core Library API


## `MakeSubscriber(const std::weak_ptr<SubscriberType> &)`

**Defined in:** `rs/subscriber.h`

**Kind:** Core Library API


## `MakeSubscription()`

**Defined in:** `rs/subscription.h`

**Kind:** Core Library API


## `MakeSubscription(RequestCb, CancelCb)`

**Defined in:** `rs/subscription.h`

**Kind:** Core Library API


## `MakeSubscription(const std::shared_ptr<SubscriptionType> &)`

**Defined in:** `rs/subscription.h`

**Kind:** Core Library API


## `Map(Mapper)`

**Defined in:** `rs/map.h`

**Kind:** Operator Builder


## `Max()`

**Defined in:** `rs/max.h`

**Kind:** Operator Builder


## `Merge(Publisher...)`

**Defined in:** `rs/merge.h`

**Kind:** Operator


## `Min()`

**Defined in:** `rs/min.h`

**Kind:** Operator Builder


## `Never()`

**Defined in:** `rs/never.h`

**Kind:** Operator


## `Pipe(Value, Operator...)`

**Defined in:** `rs/pipe.h`

**Kind:** Operator


## `Publisher<>`

**Defined in:** `rs/publisher.h`

**Kind:** Core Library API


## `PublisherBase`

**Defined in:** `rs/publisher.h`

**Kind:** Core Library API


## `Range(Value, size_t)`

**Defined in:** `rs/range.h`

**Kind:** Operator


## `Reduce(Accumulator, Reducer)`

**Defined in:** `rs/reduce.h`

**Kind:** Operator Builder


## `ReduceGet(MakeInitial, Reducer)`

**Defined in:** `rs/reduce.h`

**Kind:** Operator Builder


## `ReduceWithoutInitial(Reducer)`

**Defined in:** `rs/reduce.h`

**Kind:** Operator Builder


## `Repeat(Value, size_t)`

**Defined in:** `rs/repeat.h`

**Kind:** Operator


## `Scan(Accumulator, Mapper)`

**Defined in:** `rs/scan.h`

**Kind:** Operator Builder


## `Skip(size_t)`

**Defined in:** `rs/skip.h`

**Kind:** Operator Builder


## `SkipWhile(Predicate)`

**Defined in:** `rs/skip_while.h`

**Kind:** Operator Builder


## `Some(Predicate)`

**Defined in:** `rs/some.h`

**Kind:** Operator Builder


## `Splat(Callback)`

**Defined in:** `rs/splat.h`

**Kind:** Core Library API


## `Start(CreateValue)`

**Defined in:** `rs/start.h`

**Kind:** Operator


## `StartWith(Value)`

**Defined in:** `rs/start_with.h`

**Kind:** Operator Builder


## `StartWithGet(MakeValue...)`

**Defined in:** `rs/start_with.h`

**Kind:** Operator Builder


## `Subscriber<>`

**Defined in:** `rs/subscriber.h`

**Kind:** Core Library API


## `SubscriberBase`

**Defined in:** `rs/subscriber.h`

**Kind:** Core Library API


## `Subscription`

**Defined in:** `rs/subscription.h`

**Kind:** Core Library API


## `SubscriptionBase`

**Defined in:** `rs/subscription.h`

**Kind:** Core Library API


## `Sum()`

**Defined in:** `rs/sum.h`

**Kind:** Operator Builder


## `Take(Count)`

**Defined in:** `rs/take.h`

**Kind:** Operator Builder


## `TakeWhile(Predicate)`

**Defined in:** `rs/take_while.h`

**Kind:** Operator Builder


## `Throw(Exception)`

**Defined in:** `rs/throw.h`

**Kind:** Operator


## `Throw(const std::exception_ptr &)`

**Defined in:** `rs/throw.h`

**Kind:** Operator


## `Zip(Publisher...)`

**Defined in:** `rs/zip.h`

**Kind:** Operator
