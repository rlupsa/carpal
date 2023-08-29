# carpal

_carpal_ is a library for easying writing asynchronous functions - functions that start an opearion in background, and return control to the caller.
The asynchronous function would return a _future_ - an object that will receive the value when the asynchronous operation completes.

In addition to what the standard `std::future<T>` offers, our `carpal::Future<T>` allows enqueueing operations to be executed when the
original operation completes and, furthermore, to compose such operations in a way similar to the basic blocks of the standard programming.
In particular, we offer support for easily looping an asynchronous operation - something that is very tedious with other similar frameworks.
