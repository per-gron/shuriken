# `rs-grpc`

*rs-grpc* is a library for C++ applications and services that use gRPC. It is a layer on top of the low level asynchronous CompletionQueue API offered by gRPC++ that lets you write a lot less code and focus on application logic rather than memory management and other low-level concerns.


# Features

* Offers an opinionated high-level API for writing gRPC services in C++14.
* Everything is asynchronous.
* Uses [Reactive Streams](http://www.reactive-streams.org/), an existing well-known paradigm.
* The API is symmetric across unary, client streaming, server streaming and bidi RPCs.
* Support for cancellation, including transitive cancellation: Cancelling a RPC automatically cancels upstream calls as well, no special application code needed.
* Full support for asynchronous backpressure: No built-in unbounded buffers and no blocking writes, no special application code needed.
* Has a threading model that is friendly both to performance and application logic.


# Background

rs-grpc is fully asynchronous, but it is not a callback based library, and it is not a promise/future based library. The library is, as the name implies, built on top of [*Reactive Streams*](http://www.reactive-streams.org/) (Reactive Streams is mainly targeted at Java; rs-grpc comes with a C++ implementation that is the rs-grpc author's translation of the concepts to C++). If you have used Reactive Extensions libraries such as RxJava or Rx.NET, rs-grpc's programming model will be familiar to you.

Reactive Streams is an abstraction that is similar to futures in that both a future and a reactive stream represent an asynchronous computation, and they both have very similar means of handling asynchronous errors. Reactive Streams adds a few things on top of what a future does:

* Because of its heritage from [ReactiveX](http://reactivex.io/), the Reactive Streams library used in rs-grpc offers a library of operators on the streams. The operators are useful for a wide range of applications and are well known and [well](http://reactivex.io/documentation/operators.html)[documented](http://rxmarbles.com/).
* Unlike most futures libraries, Reactive Streams has a built-in concept of cancellation.
* Instead of providing exactly one value, a reactive stream provides zero or more values.
* The API enforces asynchronous backpressure: The programming model makes it possible to handle that the receiver is slower than the sender without blocking threads or having write buffers of unbounded size. (This is important because of the streaming nature of this abstraction; futures does not need this.)
* Reactive Streams are lazy by default: It is not until you actually use/subscribe to a stream that the asynchronous operation occurs. This enables some interesting uses.

The features that Reactive Streams add on top of futures fit gRPC very nicely: gRPC performs best when used as an asynchronous API, it has streams, it has cancellation and backpressure is an important factor for scalable services.

On the other hand, the features that Reactive Streams add on top of futures also makes it a more complex abstraction. In order to use it effectively you have to learn it. Fortunately, there are plenty of tutorials, articles and presentations online that explain how it works, and when you dig deeper and find that you need to do something that can't be done with a built-in operator or stream source, there is a clear specification that says what you need to do to make it play nicely with the rest of the system.


# Tutorial

TODO(peck): Actually write this

* Full code example of simple hello world unary RPC service and client.
  -> subsequent examples don't have the full code
* Making a request to another service
* Making multiple requests to other services
* Client streaming: Sum
* Server streaming: Range
* Bidi streaming: Cumulative sum
* Combining different types of streams / unary calls
* Basic cancellation
* Basic error handling
* Error handling when combining streams
* Error handling and cancellation
* Backpressure
* Transitive backpressure
