# `rx`

`rx` is a Reactive Streams-compatible library that offers types for reactive
streams along with reactive extensions operators on them.

* It is written using C++14 (and uses generic lambdas extensively, so it
  probably cannot be backported to older versions of C++).
* Like all Reactive Streams based libraries, it offers mandatory back-pressure
  support (RxCpp does not do this).
* It has a trivial threading model: Nothing in this library is thread safe.

## Threading model

Unlike most other Rx libraries (and future/promise libraries too, for that
matter), `rx` does not do anything at all about threads or concurrency. The only
thread safety guarantee that the library offers is that separate objects can be
used concurrently on separate threads (because it has no global mutable state).

The author of this library sees this not as a limitation but as a feature:

* There are precisely 0 thread safety bugs in this library, guaranteed.
* There is never any overhead from locking or atomic primitives that you don't
  need.
* This library never spawns threads or does anything behind your back, ever.
* The user is in full control of the threading model.

In order to use this library effectively, the application needs one or more
runloops. If there are separate runloops on separate threads, then each stream
should be confined to one given runloop.
