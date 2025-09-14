// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/StreamSource.h"
#include "carpal/Future.h"
#include "carpal/Logger.h"
#include "carpal/ThreadPool.h"

#include <catch2/catch_test_macros.hpp>
#include <stdio.h>

#include "TestHelper.h"

using namespace carpal;

carpal::StreamSource<int,bool> gen3(carpal::CoroutineSchedulingInfo const& schedulingInfo, int v) {
    co_await schedulingInfo;
    for(int i=0 ; i<3 ; ++i) {
        co_yield v+i;
    }
    co_return true;
};

carpal::StreamSource<int,void> gen3void(carpal::CoroutineSchedulingInfo const& schedulingInfo, int v) {
    co_await schedulingInfo;
    for(int i=0 ; i<3 ; ++i) {
        co_yield v+i;
    }
};

carpal::StreamSource<int> genCompound(carpal::CoroutineSchedulingInfo const& schedulingInfo, carpal::StreamSource<int,bool> gen1, carpal::StreamSource<int,void> gen2) {
    co_await schedulingInfo;
    while(true) {
        StreamValue<int,bool> v1 = co_await gen1;
        if(v1.isItem()) {
            co_yield v1.item();
        } else {
            co_return;
        }
        StreamValue<int> v2 = co_await gen2;
        if(v2.isItem()) {
            co_yield v2.item();
        } else {
            co_return;
        }
    }
}

carpal::StreamSource<int> itemCompound(carpal::CoroutineSchedulingInfo const& schedulingInfo, carpal::StreamSource<int> gen1, carpal::StreamSource<int> gen2) {
    co_await schedulingInfo;
    while(true) {
        std::optional<int> v1 = co_await gen1.nextItem();
        if(v1.has_value()) {
            co_yield v1.value();
        } else {
            co_return;
        }
        std::optional<int> v2 = co_await gen2.nextItem();
        if(v2.has_value()) {
            co_yield v2.value();
        } else {
            co_return;
        }
    }
}

carpal::StreamSource<int> gen1except(carpal::CoroutineSchedulingInfo const& schedulingInfo, int v) {
    co_await schedulingInfo;
    co_yield v+1;
    throw 123;
};

carpal::Future<bool> futureWithIterator(carpal::CoroutineSchedulingInfo const& schedulingInfo, carpal::StreamSource<int> coro1, carpal::StreamSource<int> coro2) {
    carpal::CoroutineSchedulingInfo childSchedulingInfo = schedulingInfo.parallelStart();
    co_await schedulingInfo;
    carpal::StreamSource<int> coro = itemCompound(childSchedulingInfo, std::move(coro1), std::move(coro2));
    std::vector<int> const expected = {10, 20, 11, 21, 12, 22};
    size_t i=0;
    for(auto it=co_await coro.begin() ; it != coro.end() ; co_await ++it) {
        CHECK(i<expected.size());
        if(i<expected.size()) {
            CHECK(*it == expected[i]);
        } else {
            co_return false;
        }
        ++i;
    }
    CHECK(i == expected.size());
    co_return true;
};

TEST_CASE("AsyncGenerator_basic", "[asyncGenerator]") {
    carpal::ThreadPool scheduler(2);

    carpal::StreamSource<int,bool> coro = gen3(scheduler.sameThreadStart(), 10);
    carpal::StreamValue<int,bool> e = coro.dequeue();
    CHECK(e.isItem());
    CHECK(e.item() == 10);
    e = coro.dequeue();
    CHECK(e.isItem());
    CHECK(e.item() == 11);
    e = coro.dequeue();
    CHECK(e.isItem());
    CHECK(e.item() == 12);
    e = coro.dequeue();
    CHECK(e.isEof());
    e = coro.dequeue();
    CHECK(e.isEof());
}

TEST_CASE("AsyncGenerator_void", "[asyncGenerator]") {
    carpal::ThreadPool scheduler(2);

    carpal::StreamSource<int> coro = gen3void(scheduler.sameThreadStart(), 10);
    carpal::StreamValue<int> e = coro.dequeue();
    CHECK(e.isItem());
    CHECK(e.item() == 10);
    e = coro.dequeue();
    CHECK(e.isItem());
    CHECK(e.item() == 11);
    e = coro.dequeue();
    CHECK(e.isItem());
    CHECK(e.item() == 12);
    e = coro.dequeue();
    CHECK(e.isEof());
    e = coro.dequeue();
    CHECK(e.isEof());
}

TEST_CASE("AsyncGenerator_compound", "[asyncGenerator]") {
    carpal::ThreadPool scheduler(2);

    carpal::StreamSource<int,bool> coro1 = gen3(scheduler.parallelStart(), 10);
    carpal::StreamSource<int> coro2 = gen3void(scheduler.parallelStart(), 20);
    carpal::StreamSource<int> coro = genCompound(scheduler.parallelStart(), std::move(coro1), std::move(coro2));
    std::vector<int> const expected = {10, 20, 11, 21, 12, 22};
    for(int ev : expected) {
        carpal::StreamValue<int> e = coro.dequeue();
        CHECK(e.isItem());
        CHECK(e.item() == ev);
    }
    carpal::StreamValue<int> e = coro.dequeue();
    CHECK(e.isEof());
}

TEST_CASE("AsyncGenerator_exception", "[asyncGenerator]") {
    carpal::ThreadPool scheduler(2);

    carpal::StreamSource<int> coro = gen1except(scheduler.parallelStart(), 10);
    carpal::StreamValue<int> e = coro.dequeue();
    CHECK(e.isItem());
    CHECK(e.item() == 11);
    e = coro.dequeue();
    CHECK(e.isException());
    try {
        std::rethrow_exception(e.exception());
        CHECK(false);
    } catch(int ex) {
        CHECK(ex == 123);
    }
}

TEST_CASE("AsyncGenerator_items", "[asyncGenerator]") {
    carpal::ThreadPool scheduler(2);

    carpal::StreamSource<int> coro1 = gen3void(scheduler.parallelStart(), 10);
    carpal::StreamSource<int> coro2 = gen3void(scheduler.parallelStart(), 20);
    carpal::StreamSource<int> coro = itemCompound(scheduler.parallelStart(), std::move(coro1), std::move(coro2));
    std::vector<int> const expected = {10, 20, 11, 21, 12, 22};
    for(int ev : expected) {
        carpal::StreamValue<int> e = coro.dequeue();
        CHECK(e.isItem());
        CHECK(e.item() == ev);
    }
    carpal::StreamValue<int> e = coro.dequeue();
    CHECK(e.isEof());
}

TEST_CASE("AsyncGenerator_iterator", "[asyncGenerator]") {
    carpal::ThreadPool scheduler(2);

    carpal::StreamSource<int> coro1 = gen3void(scheduler.parallelStart(), 10);
    carpal::StreamSource<int> coro2 = gen3void(scheduler.parallelStart(), 20);
    carpal::Future<bool> coro = futureWithIterator(scheduler.parallelStart(), std::move(coro1), std::move(coro2));
    CHECK(coro.get());
}
