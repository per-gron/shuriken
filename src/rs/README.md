# `rs`

`rs` is a minimalist Reactive Streams-inspired library that offers types for reactive streams along with reactive extensions operators on them.

* It is written using C++14 (and uses generic lambdas extensively, so it probably cannot be backported to older versions of C++).
* Like all Reactive Streams based libraries, it offers mandatory non-blocking back-pressure support (at the time of writing, [RxCpp](https://github.com/Reactive-Extensions/RxCpp) does not do this).
* It has a trivial threading model: Nothing in this library is thread safe.
* It attempts to be small, to avoid excessive compile times.
* It does not have the concept of a Scheduler, or time. The author of this library believes that such concepts belong in a runloop.


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

In the `rs` library, these are not concrete types; rather, they are concepts.

A *Publisher* is a provider of a potentially unbounded number of sequenced elements, publishing them according to the demand received from its Subscriber(s).

A Publisher needs to publically inherit the `PublisherBase` class and offer an
`operator()` overload that takes a Subscriber and returns a Subscription.

In response to a call to `operator()` the possible invocation sequences for methods on the `Subscriber` are given by the following protocol:

```
onNext* (onError | onComplete)?
```

This means that the subscriber will receive a possibly unbounded number of `onNext` signals (as requested with the `Request` method) followed by an `onError` signal if there is a failure, or an `onComplete` signal when no more elements are available—all as long as the `Subscription` is not cancelled.


### NOTES

- The specifications below use binding words in capital letters from https://www.ietf.org/rfc/rfc2119.txt


## Glossary

| Term                      | Definition                                                                                             |
| ------------------------- | ------------------------------------------------------------------------------------------------------ |
| <a name="term_signal">Signal</a> | As a noun: one of the `OnNext`, `OnComplete`, `OnError`, `Request(n)` or cancel (calling the destructor of the Subscription) methods. As a verb: calling/invoking a signal. |
| <a name="term_demand">Demand</a> | As a noun, the aggregated number of elements requested by a Subscriber which is yet to be delivered (fulfilled) by the Publisher. As a verb, the act of `Request`-ing more elements. |
| <a name="term_return_normally">Return normally</a> | Only ever returns a value of the declared type to the caller. The only legal way to signal failure to a `Subscriber` is via the `OnError` method.|
| <a name="term_responsivity">Responsivity</a> | Readiness/ability to respond. In this document used to indicate that the different components should not impair each others ability to respond. |
| <a name="term_non-obstructing">Non-obstructing</a> | Quality describing a method which is as quick to execute as possible—on the calling thread. This means, for example, avoids heavy computations and other things that would stall the caller´s thread of execution. |
| <a name="term_terminal_state">Terminal state</a> | For a Publisher: When `OnComplete` or `OnError` has been signalled. For a Subscriber: When an `OnComplete` or `OnError` has been received.|
| <a name="term_nop">NOP</a> | Execution that has no detectable effect to the calling thread, and can as such safely be called any number of times.|


## SPECIFICATION


### 1. Publisher ([Code](include/rs/publisher.h))

TODO(peck)


### 2. Subscriber ([Code](include/rs/subscriber.h))

TODO(peck)


### 3. Subscription ([Code](include/rs/subscription.h))

TODO(peck)



## Acknowledgements

Much of this documentation is derived from the [documentation for Reactive streams for the JVM](https://github.com/reactive-streams/reactive-streams-jvm). Thanks!
