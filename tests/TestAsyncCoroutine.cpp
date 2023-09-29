// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/AsyncCoroutine.h"
#include "carpal/Future.h"
#include "carpal/ThreadPool.h"

#include <catch2/catch.hpp>
#include <stdio.h>

#include "TestHelper.h"

using namespace carpal;

carpal::AsyncCoroutine<int> coroFunc(carpal::CoroutineScheduler*, int const& v) {
    co_return v + 1;
};

carpal::AsyncCoroutine<int> coroFunc_future(carpal::CoroutineScheduler*, Future<int> f) {
    int ret = (co_await f) + 1;
    co_return ret;
};

carpal::AsyncCoroutine<int> coroFunc_future(Future<int> f) {
    int ret = (co_await f) + 1;
    co_return ret;
};

carpal::AsyncCoroutine<int> coroFunc_future_sum(Future<int> f1, Future<int> f2) {
    int ret = (co_await f1) + (co_await f2);
    co_return ret;
};

TEST_CASE("SimpleCoroutine_async_immediate_int", "[asyncCoroutine]") {
    carpal::CoroutineScheduler scheduler;

    carpal::AsyncCoroutine<int> coro = coroFunc(&scheduler, 10);
    CHECK(coro.get() == 11);
}

TEST_CASE("SimpleCoroutine_Future_int_completed", "[asyncCoroutine]") {
    Future<int> f = completedFuture(20);
    auto coroFunc = [f]() -> AsyncCoroutine<int> {
        co_return co_await(f) + 1;
    };
    auto coro = coroFunc();
    CHECK(coro.get() == 21);
}

TEST_CASE("SimpleCoroutine_Future_int_not_completed", "[asyncCoroutine]") {
    Promise<int> p;
    Future<int> f = p.future();
    auto f1 = executeLaterVoid([p](){
        p.set(20);
    }, 200);
    auto coro = coroFunc_future(defaultCoroutineScheduler(), f);
    CHECK(coro.get() == 21);
}

TEST_CASE("SimpleCoroutine_layers", "[asyncCoroutine]") {
    Promise<int> p;
    Future<int> f = p.future();
    auto coroFunc = [](Future<int> f) -> AsyncCoroutine<int> {
        co_return co_await(f);
    };
    auto f1 = executeLaterVoid([p](){p.set(20);}, 300);
    auto coro = coroFunc_future(defaultCoroutineScheduler(), f);
    CHECK(coro.get() == 21);
    CHECK(f.get() == 20);
}

TEST_CASE("SimpleCoroutine_layers_multiple", "[asyncCoroutine]") {
    Promise<int> p1;
    Future<int> f1 = p1.future();
    Promise<int> p2;
    Future<int> f2 = p2.future();
    auto coroFunc = [](Future<int> f1, Future<int> f2) -> AsyncCoroutine<int> {
        co_return co_await(f1) + co_await(f2);
    };
    auto fx1 = executeLaterVoid([p1](){p1.set(20);});
    auto fx2 = executeLaterVoid([p2](){p2.set(20);}, 300);
    auto coro1 = coroFunc(f1, f2);
    auto coro2 = coroFunc(f1, f2);
    CHECK(coro1.get() == 40);
    CHECK(coro2.get() == 40);
}

TEST_CASE("SimpleCoroutine_layers_multithread", "[asyncCoroutine]") {
    Promise<int> p1;
    Future<int> f1 = p1.future();
    Promise<int> p2;
    Future<int> f2 = p2.future();
    auto fx1 = executeLaterVoid([p1](){
        p1.set(22);
    }, 300);
    auto fx2 = executeLaterVoid([p2](){
        p2.set(20);
    });
    auto coro1 = coroFunc_future_sum(f1, f2);
    std::thread child([f1, f2](){
        auto coro2 = coroFunc_future_sum(f1, f2);
        CHECK(coro2.get() == 42);
    });
    CHECK(coro1.get() == 42);
    child.join();
}

// This is a counter-example. SimpleCoroutine must always be scheduled on the same thread
TEST_CASE("SimpleCoroutine_layers_multithread2", "[asyncCoroutine]") {
    for(int iteration = 0 ; iteration < 20 ; ++iteration) {
        Promise<int> p1;
        Future<int> f1 = p1.future();
        Promise<int> p2;
        Future<int> f2 = p2.future();
        auto fx1 = executeLaterVoid([p1](){
            p1.set(22);
        }, 30);
        auto fx2 = executeLaterVoid([p2](){
            p2.set(20);
        });
        std::thread child([f2](){
            auto coro2 = coroFunc_future(f2);
            CHECK(coro2.get() == 21);
        });
        auto coro1 = coroFunc_future(f1);
        CHECK(coro1.get() == 23);
        child.join();
    }
}
