# `rs`

*rs* is a minimalist unofficial [Reactive Streams](http://www.reactive-streams.org/) library that offers types for reactive streams along with functions that operate on them.

* It is written in C++14.
* Like all Reactive Streams based libraries, it offers mandatory non-blocking back-pressure support (at the time of writing, [RxCpp](https://github.com/Reactive-Extensions/RxCpp) does not do this).
* It has a trivial threading model: Nothing in this library is thread safe.
* It attempts to be small, to avoid excessive compile times.
* It does not (yet?) have the concept of a Scheduler, or time, like Rx libraries do.


## Introduction

rs tries not to be an innovative library. It steals most of its ideas and names from Reactive Streams and [ReactiveX](http://reactivex.io/). It is conceptually very similar to other ReactiveX libraries, for example [RxJava](https://github.com/ReactiveX/RxJava). A lot of information about RxJava applies directly to rs. If you are unsure about what the underlying idea of the rs library is, it might help to read tutorials or watch presentations on Reactive Streams and ReactiveX.

The main entity that the rs library offers is a *Publisher*. Similar to a future or a promise, a Publisher represents an asynchronous computation. An idiomatic use of the rs libray is to make procedures that perform asynchronous operations return a Publisher, for example:

```cpp
Publisher<User> LookupUserById(const std::string &user_id);
Publisher<std::string> UsernameToUserId(const std::string &username);
```

People who are familiar with futures or promises will recognize this pattern. In many ways a Publisher is used just like a future object.

`LookupUserById` and `UsernameToUserId` above are asynchronous procedures that return immediately. The returned Publisher objects can be used to subscribe to and transform the results.


### Creating Publishers

The rs library offers helper functions to create Publisher objects. Here are some of them:

* `Empty()` returns a Publisher that emits no values when subscribed to.
* `Just(args...)` returns a Publisher that emits just the given values. `Just()` is equivalent to `Empty()` and `Just(1, 2)` emits 1 and 2.
* `From(container)` returns a Publisher that emits all the values in the provided STL-style container, for example an `std::vector`.
* `Range(start, count)` returns a Publisher that counts upwards from `start`. For example, `Range(2, 3)` emits 2, 3 and 4.

These are often handy but none of them are asynchronous sources. This is because rs in itself does not do anything asynchronous. In practice, rs will often be used together with a library that offers asynchronous Publisher sources. For example, the *rs-grpc* library provides Publishers that asynchronously emit the responses of gRPC calls.

It is also possible but not very common in application code to create custom Publishers from scratch. The rs specification provides detailed information about the exact requirements of custom Publisher types.


### Manipulating Publishers

rs has a rich library of operators on Publisher objects. For example, the `Map` operator modifies each element in a stream, much like the functional programming map function works for lists:

```cpp
// reverse_username is a functor that takes a Publisher of User objects and
// returns a Publisher of User objects.
auto reverse_username = Map([](User &&user) {
  auto username = user.username();
  std::reverse(username.begin(), username.end());
  user.set_username(username);
  return user;
});

Publisher<User> user = LookupUserById("123");

// user_with_reversed_username is a Publisher that emits User
auto user_with_reversed_username = reverse_username(user);
```

Because it is very common to plumb several Publisher operators together, rs offers a `Pipe` operator:

```cpp
// This does the same as the code above
auto user_with_reversed_username = Pipe(
    LookupUserById("123"),
    Map([](User &&user) {
      auto username = user.username();
      std::reverse(username.begin(), username.end());
      user.set_username(username);
      return user;
    }));
```

`Pipe(a, b, c)` is basically the same as writing `c(b(a))`.

Another useful operator is `FlatMap`, which is similar to `Map` but it allows the mapper function to return a Publisher. It is useful for chaining asynchronous operations:

```cpp
auto user = Pipe(
    UsernameToUserId("john_doe"),
    FlatMap([](const std::string &user_id) {
      return LookupUserById(user_id);
    }));
```

Or, more concisely:

```cpp
auto user = Pipe(
    UsernameToUserId("john_doe"),
    FlatMap(&LookupUserById));
```

One of the best features of the rs library is that operators compose very well:

```cpp
// sum is a Publisher that emits one value: The sum of squares of all even
// numbers between 1 and 100.
auto sum = Pipe(
    Range(1, 100),
    Filter([](int x) { return (x % 2) == 0; }),
    Map([](int x) { return x * x; }),
    Sum());
```


### Subscribing to Publishers

Once you have created a Publisher and applied the necessary operations on it, you will most likely want to access the values of the stream.

Most applications do not directly subscribe to Publishers. Instead, they let some library or framework do that for them. In rs-grpc, for example, RPC handler functions return a Publisher, which the rs-grpc library subscribes to.

It is also possible to subscribe directly to Publishers. Here is an example of doing that:

```cpp
auto subscription = int_publisher.Subscribe(MakeSubscriber(
    [](int value) {
      printf("Got value: %d\n", value);
    },
    [](std::exception_ptr &&error) {
      printf("Something went wrong\n");
    },
    [] {
      printf("The stream completed successfully\n");
    }));
// It is possible to request only some elements from the stream at a time. This
// can be useful if there is a risk that the publisher could produce data
// faster than the subscriber can handle it.
//
// No elements are provided to the Subscriber until they are requested. Here,
// an unbounded number of elements are requested, which effectively gives the
// Publisher a green light to push elements as quickly as it can.
subscription.Request(ElementCount::Unbounded());
```


### The (absence of a) `Publisher` type

In the Java version of Reactive Streams [`Publisher`](https://github.com/reactive-streams/reactive-streams-jvm/blob/master/api/src/main/java/org/reactivestreams/Publisher.java), [`Subscriber`](https://github.com/reactive-streams/reactive-streams-jvm/blob/master/api/src/main/java/org/reactivestreams/Subscriber.java) and [`Subscription`](https://github.com/reactive-streams/reactive-streams-jvm/blob/master/api/src/main/java/org/reactivestreams/Subscription.java) are Java interfaces. Looking at those interfaces, one might expect to find pure virtual `Publisher`, `Subscriber` and `Subscription` classes in rs, but there are no such classes.

Instead, in rs, Publisher, Subscriber and Subscription are *concepts*, much like iterators in C++. An rs Publisher is any C++ type that fulfills the requirements of the Publisher concept.

This can be a bit counterintuitive at first. For example, the return type of `Just(1)` is not `Publisher<int>`; it is a type that doesn't even have a name (because the type contains the type of a lambda expression). The public API of rs does not give a name for the type of `Just(1)`, it only promises that it conforms to the Publisher concept.

This design allows rs to be very efficient when chaining operators together: The compiler can see and inline chains of operators as it sees fit, since there aren't virtual method calls that hide things.


### Eraser types

Although the concept-based (as opposed to pure virtual class-based) design of rs can offer great performance, it can sometimes force code to expose more type information to its callers than desired. As an example, let's look at a function `EvenSquares(int n)` that that computes the sum of squares of even numbers between 1 and `n`:

```cpp
auto EvenSquares(int n) {
  return Pipe(
      Range(1, n),
      Filter([](int x) { return (x % 2) == 0; }),
      Map([](int x) { return x * x; }),
      Sum());
}
```

In this code, the return type of `EvenSquares` doesn't have a name. In fact, the easiest way to give a name to it would probably be to write `decltype(... the function body goes here ...))`. If this was to be exposed as a library function the implementation would have to be in the header file, which is not nice.

To solve this type of problem, rs offers *eraser types* for the Publisher, Subscriber and Subscription concepts:

```cpp
Publisher<int> EvenSquares(int n) {
  return Publisher<int>(Pipe(
      Range(1, n),
      Filter([](int x) { return (x % 2) == 0; }),
      Map([](int x) { return x * x; }),
      Sum()));
}
```

Unlike the first version of `EvenSquares`, this one can easily live in a `.cpp` implementation file.

`Publisher<int>` is a class that can be constructed with any Publisher and behaves just like the one it was created with. The only difference is that it "hides" the type of the underlying Publisher (using virtual method calls).

In addition to `Publisher`, there are also type erasers for Subscribers and Subscriptions.

It is possible to construct Publishers that can emit more than one type (and correspondingly Subscribers that can receive more than one type). They can also be type erased, for example with `Publisher<int, std::string>`. Publishers that never emit a value and only ever complete or fail can be type erased with `Publisher<>`.


### Concept predicates

There are type predicates that can be used to check if a type claims to conform to a specific concept:

* `IsPublisher<T>` is a `constexpr bool` that is true if `T` is a Publisher.
* `IsSubscriber<T>` is a `constexpr bool` that is true if `T` is a Subscriber.
* `IsSubscription<T>` is a `constexpr bool` that is true if `T` is a Subscription.




## Threading model

Unlike most other Rx libraries (and future/promise libraries too, for that matter), `rs` does not do anything at all about threads or concurrency. The only thread safety guarantee that the library offers is that separate objects can be used concurrently on separate threads (because it has no global mutable state).

The author of this library sees this not as a limitation but as a feature:

* There are precisely 0 thread safety bugs in this library, guaranteed.
* There is never any overhead from locking or atomic primitives that you don't need.
* This library never spawns threads or does anything behind your back, ever.
* The user is in full control of the threading model.

In order to use this library effectively, the application needs one or more runloops. If there are separate runloops on separate threads, then each stream should be confined to one given runloop.


## Goals, Design and Scope

Please refer to [the Reactive Streams for JVM's documentation](https://github.com/reactive-streams/reactive-streams-jvm#goals-design-and-scope).


## API Components

The API consists of the following components:

1. Publisher
2. Subscriber
3. Subscription

In the rs library, these are not concrete types; rather, they are concepts: Any type can be a Publisher, a Subscriber or a Subscription as long as they fulfill all the requirements of that concept.

There are type predicates for each concept:

* `IsPublisher<T>` is a `constexpr` expression that evaluates to `true` if `T` claims to be a Publisher, otherwise `false`.
* `IsSubscriber<T>` is a `constexpr` expression that evaluates to `true` if `T` claims to be a Subscriber, otherwise `false`.
* `IsSubscription<T>` is a `constexpr` expression that evaluates to `true` if `T` claims to be a Subscription, otherwise `false`.

It is sometimes useful to have a concrete type that can hold an object that is a Publisher, a Subscriber or a Subscription. Since these concepts don't have a single concrete type this is not possible to do directly. To make that possible, there are type erasure wrappers for each concept:

`Publisher<T> erased_publisher(publisher)` defines a `Publisher<T>` object that is itself a Publisher from `publisher`, regardless of which concrete type `publisher` has. This can be useful to be able to return a Publisher object from a C++ implementation file without having to expose which type of Publisher it is in the header file.

Similarly, there are `Subscriber<T>` and `Subscription` type erasure wrappers for Subscriber and Subscription.

The type erasure wrappers use `virtual` method calls, so using them incurs some overhead.


## Specification for the Publisher, Subscriber and Subscription concepts


### Glossary

The specifications below use binding words in capital letters from https://www.ietf.org/rfc/rfc2119.txt

| Term                      | Definition                                                                                             |
| ------------------------- | ------------------------------------------------------------------------------------------------------ |
| <a name="term_signal">Signal</a> | As a noun: one of the `OnNext`, `OnComplete`, `OnError`, `Request(n)`, `Cancel` methods. As a verb: calling/invoking a signal. |
| <a name="term_demand">Demand</a> | As a noun, the aggregated number of elements requested by a Subscriber which is yet to be delivered (fulfilled) by the Publisher. As a verb, the act of `Request`-ing more elements. |
| <a name="term_return_normally">Return normally</a> | Only ever returns a value of the declared type to the caller. The only legal way to signal failure to a `Subscriber` is via the `OnError` method.|
| <a name="term_responsivity">Responsivity</a> | Readiness/ability to respond. In this document used to indicate that the different components should not impair each others ability to respond. |
| <a name="term_non-obstructing">Non-obstructing</a> | Quality describing a method which is as quick to execute as possible—on the calling thread. This means, for example, avoids heavy computations and other things that would stall the caller´s thread of execution. |
| <a name="term_terminal_state">Terminal state</a> | For a Publisher: When `OnComplete` or `OnError` has been signalled. For a Subscriber: When an `OnComplete` or `OnError` has been received. |
| <a name="term_nop">NOP</a> | Execution that has no detectable effect to the calling thread, and can as such safely be called any number of times. |


### 1. Publisher ([Code](include/rs/publisher.h))

A *Publisher* is a provider of a potentially unbounded number of sequenced elements, publishing them according to the demand received from its Subscriber(s). Having a Publisher does not in itself mean that any data is being streamed; many Publishers wait with doing anything until the `Subscribe` method is invoked.

A Publisher MUST publicly inherit the `PublisherBase` class and MUST offer a `Subscribe` method for subscribing to the stream that takes an rvalue reference to a Subscriber and returns a Subscription. Because there is no concrete for all Subscribers, this method must be a template.

That `Subscribe` takes an rvalue reference to a Subscription implies that the Publisher takes over ownership of the Subscriber when it is subscribed to.

In response to a call to `Subscribe` the possible invocation sequences for methods on the `Subscriber` are given by the following protocol:

```
OnNext* (OnError | OnComplete)? ~Subscriber
```

This means that the subscriber will receive a possibly unbounded number of `OnNext` signals (as requested with the `Request` method) followed by an `OnError` signal if there is a failure, or an `OnComplete` signal when no more elements are available—all as long as the `Subscription` is not cancelled. Finally, the Subscriber is destroyed.

| ID                        | Rule                                                                                                   |
| ------------------------- | ------------------------------------------------------------------------------------------------------ |
| <a name="1.1">1</a>       | The total number of `OnNext`´s signalled by a `Publisher` to a `Subscriber` MUST be less than or equal to the total number of elements requested by that `Subscriber`´s `Subscription` at all times. |
| [:bulb:](#1.1 "1.1 explained") | *The intent of this rule is to make it clear that Publishers cannot signal more elements than Subscribers have requested. There’s an implicit, but important, consequence to this rule: Since demand can only be fulfilled after it has been received, there’s a happens-before relationship between requesting elements and receiving elements.* |
| <a name="1.2">2</a>       | A `Publisher` MAY signal fewer `OnNext` than requested and terminate the `Subscription` by calling `OnComplete` or `OnError`. |
| [:bulb:](#1.2 "1.2 explained") | *The intent of this rule is to make it clear that a Publisher cannot guarantee that it will be able to produce the number of elements requested; it simply might not be able to produce them all; it may be in a failed state; it may be empty or otherwise already completed.* |
| <a name="1.3">3</a>       | `OnNext`, `OnError` and `OnComplete` signaled to a `Subscriber` MUST be signaled in a `thread-safe` manner—and if performed by multiple threads—use [external synchronization](#term_ext_sync). |
| [:bulb:](#1.3 "1.3 explained") | *The intent of this rule is to make it clear that [external synchronization](#term_ext_sync) must be employed if the Publisher intends to send signals from multiple/different threads.* |
| <a name="1.4">4</a>       | If a `Publisher` fails it MUST signal an `OnError`. |
| [:bulb:](#1.4 "1.4 explained") | *The intent of this rule is to make it clear that a Publisher is responsible for notifying its Subscribers if it detects that it cannot proceed—Subscribers must be given a chance to clean up resources or otherwise deal with the Publisher´s failures.* |
| <a name="1.5">5</a>       | If a `Publisher` terminates successfully (finite stream) it MUST signal an `OnComplete`. |
| [:bulb:](#1.5 "1.5 explained") | *The intent of this rule is to make it clear that a Publisher is responsible for notifying its Subscribers that it has reached a [terminal state](#term_terminal-state)—Subscribers can then act on this information; clean up resources, etc.* |
| <a name="1.6">6</a>       | If a `Publisher` signals either `OnError` or `OnComplete` on a `Subscriber`, that `Subscriber`’s `Subscription` MUST be considered cancelled. |
| [:bulb:](#1.6 "1.6 explained") | *The intent of this rule is to make sure that a Subscription is treated the same no matter if it was cancelled, the Publisher signalled OnError or OnComplete.* |
| <a name="1.7">7</a>       | Once a [terminal state](#term_terminal-state) has been signaled (`OnError`, `OnComplete`) it is REQUIRED that no further signals occur. |
| [:bulb:](#1.7 "1.7 explained") | *The intent of this rule is to make sure that OnError and OnComplete are the final states of an interaction between a Publisher and Subscriber pair.* |
| <a name="1.8">8</a>       | If a `Subscription` is cancelled its `Subscriber` MUST eventually stop being signaled. |
| [:bulb:](#1.8 "1.8 explained") | *The intent of this rule is to make sure that Publishers respect a Subscriber’s request to cancel a Subscription when Subscription.Cancel() has been called. The reason for *eventually* is because signals can have propagation delay due to being asynchronous.* |
| <a name="1.9">9</a>       | `Publisher.Subscribe` MUST [return normally](#term_return_normally). The only legal way to signal failure (or reject the `Subscriber`) is by calling `OnError`. |
| [:bulb:](#1.9 "1.9 explained") | *The intent of this rule is to make sure that subscribing to a Publisher never throws. |
| <a name="1.10">10</a>     | A `Publisher` MAY support multiple `Subscriber`s and decides whether each `Subscription` is unicast or multicast. |
| [:bulb:](#1.10 "1.10 explained") | *The intent of this rule is to give Publisher implementations the flexibility to decide how many, if any, Subscribers they will support, and how elements are going to be distributed.* |
| <a name="1.11">11</a>     | A `Publisher` MUST make sure that the subscriptions keep functioning after the Publisher has been destroyed. |
| [:bulb:](#1.11 "1.11 explained") | *A Publisher object's lifetime does not necessesarily extend beyond the subscriptions that it has created. Users of a Publisher MAY assume that creating a short-lived Publisher object does not cause subscriptions to be prematurely cancelled.* |



### 2. Subscriber ([Code](include/rs/subscriber.h))

A *Subscriber* is a receiver of a potentially unbounded number of sequenced elements. In order to get to the elements of a Publisher, it needs to be given a Subscriber, which it will then notify of its elements.

A Subscriber MUST publicly inherit the `SubscriberBase` class, MUST be movable and MUST have the following methods:

* `void OnNext(T &&t);`: One such method for each type that the subscriber accepts. There could potentially be zero `OnNext` methods.
* `void OnError(std::exception_ptr &&error);`
* `void OnComplete();`

| ID                        | Rule                                                                                                   |
| ------------------------- | ------------------------------------------------------------------------------------------------------ |
| <a name="2.1">1</a>       | A `Subscriber` MUST indicate demand via `Subscription.Request(ElementCount n)` to receive `OnNext` signals. |
| [:bulb:](#2.1 "2.1 explained") | *The intent of this rule is to establish that it is the responsibility of the Subscriber to indicate when, and how many, elements it is able and willing to receive.* |
| <a name="2.2">2</a>       | If a `Subscriber` suspects that its processing of signals will negatively impact its `Publisher`’s responsivity, it is RECOMMENDED that it asynchronously dispatches its signals. |
| [:bulb:](#2.2 "2.2 explained") | *The intent of this rule is that a Subscriber should [not obstruct](#term_non-obstructing) the progress of the Publisher from an execution point-of-view. In other words, the Subscriber should not starve the Publisher from CPU cycles.* |
| <a name="2.3">3</a>       | `Subscriber.OnComplete()` and `Subscriber.OnError(std::exception_ptr &&error)` MUST NOT call any methods on the `Subscription` or the `Publisher`. |
| [:bulb:](#2.3 "2.3 explained") | *The intent of this rule is to prevent cycles and race-conditions—between Publisher, Subscription and Subscriber—during the processing of completion signals.* |
| <a name="2.4">4</a>       | `Subscriber.OnComplete()` and `Subscriber.OnError(std::exception_ptr &&error)` MUST consider the Subscription cancelled after having received the signal. |
| [:bulb:](#2.4 "2.4 explained") | *The intent of this rule is to make sure that Subscribers respect a Publisher’s [terminal state](#term_terminal-state) signals. A Subscription is simply not valid anymore after an OnComplete or OnError signal has been received.* |
| <a name="2.5">5</a>       | A `Subscriber` MUST call `Subscription.Cancel()`if the `Subscription` is no longer needed. |
| [:bulb:](#2.5 "2.5 explained") | *The intent of this rule is to establish that Subscribers cannot just leave Subscriptions when they are no longer needed, they have to call `Cancel` so that resources held by that Subscription can be reclaimed in a timely manner. An example of this would be a Subscriber which is only interested in a specific element, which would then cancel its Subscription to signal its completion to the Publisher.* |
| <a name="2.6">6</a>       | A `Subscriber` MUST ensure that all calls on its `Subscription` take place from the same thread or provide for respective [external synchronization](#term_ext_sync). |
| [:bulb:](#2.6 "2.6 explained") | *The intent of this rule is to establish that [external synchronization](#term_ext_sync) must be added if a Subscriber will be using a Subscription concurrently by two or more threads.* |
| <a name="2.7">7</a>       | A `Subscriber` MUST be prepared to receive one or more `OnNext` signals after having cancelled its `Subscription` if there are still requested elements pending [see [3.11](#3.11)]. Cancelling a `Subscription` does not guarantee to perform the underlying cleaning operations immediately. |
| [:bulb:](#2.7 "2.7 explained") | *The intent of this rule is to highlight that there may be a delay between cancelling a `Subscription` and the Publisher seeing that.* |
| <a name="2.8">8</a>       | A `Subscriber` MUST be prepared to receive an `OnComplete` signal with or without a preceding `Subscription.Request(ElementCount n)` call. |
| [:bulb:](#2.8 "2.8 explained") | *The intent of this rule is to establish that completion is unrelated to the demand flow—this allows for streams which complete early, and obviates the need to *poll* for completion.* |
| <a name="2.9">9</a>     | A `Subscriber` MUST be prepared to receive an `OnError` signal with or without a preceding `Subscription.Request(ElementCount n)` call. |
| [:bulb:](#2.9 "2.9 explained") | *The intent of this rule is to establish that Publisher failures may be completely unrelated to signalled demand. This means that Subscribers do not need to poll to find out if the Publisher will not be able to fulfill its requests.* |
| <a name="2.10">10</a>     | A `Subscriber` MUST make sure that all calls on its [signal](#term_signal) methods happen-before the processing of the respective signals. I.e. the Subscriber must take care of properly publishing the signal to its processing logic. |
| [:bulb:](#2.10 "2.10 explained") | *The intent of this rule is to establish that it is the responsibility of the Subscriber implementation to make sure that asynchronous processing of its signals are thread safe. See [JMM definition of Happens-Before in section 17.4.5](https://docs.oracle.com/javase/specs/jls/se8/html/jls-17.html#jls-17.4.5).* |
| <a name="2.11">11</a>     | Calling `OnSubscribe`, `OnNext`, `OnError` or `OnComplete` MUST [return normally](#term_return_normally). The only legal way for a `Subscriber` to signal failure is by cancelling its `Subscription`. In the case that this rule is violated, any associated `Subscription` to the `Subscriber` MUST be considered as cancelled, and the caller MUST raise this error condition in a fashion that is adequate for the runtime environment. |
| [:bulb:](#2.11 "2.11 explained") | *The intent of this rule is to establish the semantics for the methods of Subscriber and what the Publisher is allowed to do in which case this rule is violated. «Raise this error condition in a fashion that is adequate for the runtime environment» could mean logging the error—or otherwise make someone or something aware of the situation—as the error cannot be signalled to the faulty Subscriber.* |


### 3. Subscription ([Code](include/rs/subscription.h))

A *Subscription* is a handle that is provided by the Publisher when a subscription is made against it. Calling `Request` signals to the publisher that the Subscriber is ready to receive data. Calling `Cancel` will make the Publisher eventually stop emitting elements to the Subscriber.

A Subscriber MUST publicly inherit the `SubscriberBase` class, MUST be movable and MUST have the following methods:

* `void Request(ElementCount count);`
* `void Cancel();`

| ID                        | Rule                                                                                                   |
| ------------------------- | ------------------------------------------------------------------------------------------------------ |
| <a name="3.1">1</a>       | `Subscription.Request`, `Subscription.Cancel` MUST only be called inside of its `Subscriber` context. |
| [:bulb:](#3.1 "3.1 explained") | *The intent of this rule is to establish that a Subscription represents the unique relationship between a Subscriber and a Publisher [see [2.12](#2.12)]. The Subscriber is in control over when elements are requested and when more elements are no longer needed.* |
| <a name="3.2">2</a>       | The `Subscription` MUST allow the `Subscriber` to call `Subscription.Request` synchronously from within `OnNext`. |
| [:bulb:](#3.2 "3.2 explained") | *The intent of this rule is to make it clear that implementations of `Request` must be reentrant, to avoid stack overflows in the case of mutual recursion between `Request` and `OnNext` (and eventually `OnComplete` / `OnError`). This implies that Publishers can be `synchronous`, i.e. signalling `OnNext`:s on the thread which calls `Request`.* |
| <a name="3.3">3</a>       | `Subscription.Request` MUST place an upper bound on possible synchronous recursion between `Publisher` and `Subscriber`. |
| [:bulb:](#3.3 "3.3 explained") | *The intent of this rule is to complement [see [3.2](#3.2)] by placing an upper limit on the mutual recursion between `Request` and `OnNext` (and eventually `OnComplete` / `OnError`). Implementations are RECOMMENDED to limit this mutual recursion to a depth of `1` (ONE)—for the sake of conserving stack space. An example for undesirable synchronous, open recursion would be Subscriber.OnNext -> Subscription.Request -> Subscriber.OnNext -> …, as it otherwise will result in blowing the calling Thread´s stack.* |
| <a name="3.4">4</a>       | `Subscription.Request` SHOULD respect the responsivity of its caller by returning in a timely manner. |
| [:bulb:](#3.4 "3.4 explained") | *The intent of this rule is to establish that `Request` is intended to be a [non-obstructing](#term_non-obstructing) method, and should be as quick to execute as possible on the calling thread, so avoid heavy computations and other things that would stall the caller´s thread of execution.* |
| <a name="3.5">5</a>       | `Subscription.Cancel` MUST respect the responsivity of its caller by returning in a timely manner. |
| [:bulb:](#3.5 "3.5 explained") | *The intent of this rule is to establish that `Cancel` is intended to be a [non-obstructing](#term_non-obstructing) method, and should be as quick to execute as possible on the calling thread, so avoid heavy computations and other things that would stall the caller´s thread of execution.* |
| <a name="3.6">6</a>       | After the `Subscription` is cancelled, additional `Subscription.Request(ElementCount n)` must be MUST be [NOPs](#term_nop). |
| [:bulb:](#3.6 "3.6 explained") | *The intent of this rule is to establish a causal relationship between cancellation of a subscription and the subsequent non-operation of requesting more elements.* |
| <a name="3.7">7</a>       | After the `Subscription` is cancelled, additional `Subscription.cancel()` MUST be [NOPs](#term_nop). |
| [:bulb:](#3.7 "3.7 explained") | *The intent of this rule is superseded by [3.5](#3.5).* |
| <a name="3.8">8</a>       | While the `Subscription` is not cancelled, `Subscription.Request(ElementCount n)` MUST register the given number of additional elements to be produced to the respective subscriber. |
| [:bulb:](#3.8 "3.8 explained") | *The intent of this rule is to make sure that `Request`-ing is an additive operation, as well as ensuring that a request for elements is delivered to the Publisher.* |
| <a name="3.9">9</a>     | While the `Subscription` is not cancelled, `Subscription.Request(ElementCount n)` MAY synchronously call `OnNext` on this (or other) subscriber(s). |
| [:bulb:](#3.9 "3.9 explained") | *The intent of this rule is to establish that it is allowed to create synchronous Publishers, i.e. Publishers who execute their logic on the calling thread.* |
| <a name="3.10">10</a>     | While the `Subscription` is not cancelled, `Subscription.Request(ElementCount n)` MAY synchronously call `OnComplete` or `OnError` on this (or other) subscriber(s). |
| [:bulb:](#3.10 "3.10 explained") | *The intent of this rule is to establish that it is allowed to create synchronous Publishers, i.e. Publishers who execute their logic on the calling thread.* |
| <a name="3.11">11</a>     | While the `Subscription` is not cancelled, `Subscription.Cancel` MUST request the `Publisher` to eventually stop signaling its `Subscriber`. The operation is NOT REQUIRED to affect the `Subscription` immediately. |
| [:bulb:](#3.11 "3.11 explained") | *The intent of this rule is to establish that the desire to cancel a Subscription is eventually respected by the Publisher, acknowledging that it may take some time before the signal is received.* |
| <a name="3.12">12</a>     | While the `Subscription` is not cancelled, `Subscription.Cancel` MUST request the `Publisher` to eventually drop any references to the corresponding subscriber. |
| [:bulb:](#3.12 "3.12 explained") | *The intent of this rule is to make sure that Subscribers can be properly destroyed after their subscription no longer being valid.* |
| <a name="3.13">13</a>     | While the `Subscription` is not cancelled, `Subscription.Cancel` MAY cause the `Publisher`, if stateful, to transition into the `shut-down` state if no other `Subscription` exists at this point [see [1.8](#1.8)]. |
| [:bulb:](#3.13 "3.13 explained") | *The intent of this rule is to allow for Publishers to signal `OnComplete` or `OnError` for new Subscribers in response to a cancellation signal from an existing Subscriber.* |
| <a name="3.15">15</a>     | Calling `Subscription.Cancel` MUST [return normally](#term_return_normally). |
| [:bulb:](#3.15 "3.15 explained") | *The intent of this rule is to disallow implementations to throw exceptions in response to `Cancel` being called.* |
| <a name="3.15">15</a>     | Calling `Subscription.Request` MUST [return normally](#term_return_normally). |
| [:bulb:](#3.15 "3.15 explained") | *The intent of this rule is to disallow implementations to throw exceptions in response to `Request` being called.* |
| <a name="3.16">16</a>     | A `Subscription` MUST support an unbounded number of calls to `Request` and MUST support a demand up to 2^63-1 (`std::numeric_limits<long long>::max()`). A demand equal or greater than 2^63-1 (`std::numeric_limits<long long>::max()`) MAY be considered by the `Publisher` as “effectively unbounded”. |
| [:bulb:](#3.16 "3.16 explained") | *The intent of this rule is to establish that the Subscriber can request an unbounded number of elements, in any increment above 0, in any number of invocations of `Request`. As it is not feasibly reachable with current or foreseen hardware within a reasonable amount of time to fulfill a demand of 2^63-1, it is allowed for a Publisher to stop tracking demand beyond this point.* |


## Subscriber controlled queue bounds

One of the underlying design principles is that all buffer sizes are to be bounded and these bounds must be *known* and *controlled* by the subscribers (subscriber in this context refers to the entity that called the subscribe method on the Publisher, likely but not necessarily the same thing as the Subscriber object). These bounds are expressed in terms of *element count* (which in turn translates to the invocation count of onNext). Any implementation that aims to support infinite streams (especially high output rate streams) needs to enforce bounds all along the way to avoid out-of-memory errors and constrain resource usage in general.

Since back-pressure is mandatory the use of unbounded buffers can be avoided. In general, the only time when a queue might grow without bounds is when the publisher side maintains a higher rate than the subscriber for an extended period of time, but this scenario is handled by backpressure instead.

Queue bounds can be controlled by a subscriber indicating demand for the appropriate number of elements. At any point in time the subscriber knows:

* the total number of elements requested: `P`
* the number of elements that have been processed: `N`

Then the maximum number of elements that may arrive—until more demand is indicated to the Publisher—is `P - N`. In the case that the subscriber also knows the number of elements B in its input buffer then this bound can be refined to `P - B - N`.

These bounds must be respected by a publisher independent of whether the source it represents can be backpressured or not. In the case of sources whose production rate cannot be influenced—for example clock ticks or mouse movement—the publisher must choose to either buffer or drop elements to obey the imposed bounds.

Subscribers indicating a demand for one element after the reception of an element effectively implement a Stop-and-Wait protocol where the demand indication is equivalent to acknowledgement. By providing demand for multiple elements the cost of acknowledgement is amortized. It is worth noting that the subscriber is allowed to indicate demand at any point in time, allowing it to avoid unnecessary delays between the publisher and the subscriber (i.e. keeping its input buffer filled without having to wait for full round-trips).


## Acknowledgements

Much of this documentation is derived from the [documentation for Reactive streams for the JVM](https://github.com/reactive-streams/reactive-streams-jvm). Thanks!
