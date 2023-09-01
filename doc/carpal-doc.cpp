// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

/** @mainpage @c carpal Library documentation
 * 
 * @section overview Overview
 * 
 * carpal is a library for easying writing asynchronous functions - functions that start an opearion in background, and return control to the caller.
 * The asynchronous function would return a @e future - an object that will receive the value when the asynchronous operation completes.
 * In addition to what the standard std::future<T> offers, our carpal::Future<T> allows enqueueing operations to be executed when the
 * original operation completes and, furthermore, to compose such operations in a way similar to the basic blocks of the standard programming.
 * In particular, we offer support for easily looping an asynchronous operation - something that is very tedious with other similar frameworks.
 * 
 * carpal::PromiseFuturePairBase is the main class, representing the (future) result of an asynchronous operation. It is (normally) initiall non-completed, and may be completed exactly once, either
 * normally or with an exception. It is designed to be refered through std::shared_ptr.
 * 
 * If the asynchronous operation returns a result of type T, then a carpal::PromiseFuturePair<T> (which derive from PromiseFuturePairBase) is to be used instead.
 * 
 * Neither of the above should be passed around directly. An asynchronous function should return a carpal::Future<T>, which is a wrapper over
 * std::shared_ptr<carpal::PromiseFuturePair<T> > and
 * provides convenience functions for the consumer of the result. In particular, Future<T> offers functionality to compose multiple asynchronous functions (or synchronous ones) -- see
 * carpal::Future::then(), carpal::Future<T>::thenAsync(), Future<T>::thenAsyncLoop(), Future<T>::thenCatch(), Future<T>::thenCatchAsync(), or the stand-alone function whenAll(). All these functions
 * allow to set up other operations to be executed after the given future completes.
 * 
 * To allow a programmer to write other functions, there is a function PromiseFuturePair<T>::addSynchronousCallback() that adds a callback to be called when the PromiseFuturePair<T> completes.
 * The callback is synchronous - which poses some limitations on what it may do. Most often, the callback will schedule some longer operation to be executed by some carpal::Executor mechanism.
 * 
 * A carpal::Promise<T> is the producer view of a carpal::PromiseFuturePair<T>. The carpal::Promise<T> constructor automatically creates the PromiseFuturePair<T> and holds it via a std::shared_ptr . It also
 * allows setting a value or an exception into the carpal::PromiseFuturePair<T> and obtaining a Future<T> pointing to the same carpal::PromiseFuturePair<T>.
 * 
 * Additional components include
 *   carpal::AlarmClock - which allows to set up operations to be executed at some given time
 *   carpal::FutureWaiter - which allows to keep a bunch of Future<T> objects produced continuosly during application execution, so that they are not lost before completion and they can all be waited for
 *     when needed.
 * 
 * */