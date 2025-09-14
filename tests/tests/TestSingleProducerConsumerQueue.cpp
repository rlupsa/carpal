// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/SingleProducerConsumerQueue.h"
#include "carpal/ThreadPool.h"

#include <catch2/catch_test_macros.hpp>
#include <stdio.h>

#include "TestHelper.h"

TEST_CASE("StreamValue_basic", "[queue]") {
    carpal::StreamValue<int,int> elEmpty;
    CHECK(!elEmpty.hasValue());

    carpal::StreamValue<int,int> el = carpal::StreamValue<int,int>::makeItem(42);
    CHECK(el.hasValue());
    CHECK(el.isItem());
    CHECK(el.item() == 42);
    el = carpal::StreamValue<int,int>::makeEof(33);
    CHECK(el.hasValue());
    CHECK(!el.isItem());
    CHECK(el.isEof());
    CHECK(el.eof() == 33);

    carpal::StreamValue<int> el2 = carpal::StreamValue<int>::makeItem(42);
    CHECK(el2.hasValue());
    CHECK(el2.isItem());
    CHECK(el2.item() == 42);
    el2 = carpal::StreamValue<int>::makeEof();
    CHECK(el2.hasValue());
    CHECK(!el2.isItem());
    CHECK(el2.isEof());
}

TEST_CASE("SingleProducerSingleConsumeQueue_basic", "[queue]") {
    carpal::SingleProducerSingleConsumerQueue<int,int> q;
    CHECK(!q.isValueAvailable());
    CHECK(q.isSlotAvailable());
    q.enqueue(carpal::StreamValue<int,int>::makeItem(10));
    CHECK(q.isValueAvailable());

    int count = 0;
    carpal::StreamValue<int,int> v;
    auto callback = [&count,&q,&v]() {
        ++count;
        v = q.dequeue();
    };
    q.setOnValueAvailableOnceCallback(callback);
    CHECK(count == 1);
    CHECK(v.isItem());
    CHECK(v.item() == 10);
    q.setOnValueAvailableOnceCallback(callback);
    CHECK(count == 1);
    q.enqueue(carpal::StreamValue<int,int>::makeItem(14));
    CHECK(count == 2);
    CHECK(v.isItem());
    CHECK(v.item() == 14);
    q.enqueue(carpal::StreamValue<int,int>::makeItem(21));
    CHECK(count == 2);
    q.setOnValueAvailableOnceCallback(callback);
    CHECK(count == 3);
    CHECK(v.isItem());
    CHECK(v.item() == 21);

    carpal::Future<void> f = executeLaterVoid([&q](){q.enqueue(carpal::StreamValue<int,int>::makeItem(33));});
    CHECK(!q.isValueAvailable());
    v = q.dequeue();
    CHECK(!q.isValueAvailable());
    CHECK(v.isItem());
    CHECK(v.item() == 33);
    f.wait();
}

TEST_CASE("SingleProducerSingleConsumerQueue_eof", "[queue]") {
    carpal::SingleProducerSingleConsumerQueue<int,int> q;
    CHECK(!q.isValueAvailable());
    q.enqueue(carpal::StreamValue<int,int>::makeEof(10));
    CHECK(q.isValueAvailable());
    carpal::StreamValue<int,int> v = q.dequeue();
    CHECK(!v.isItem());
    CHECK(v.isEof());
    CHECK(v.eof() == 10);

    CHECK(q.isValueAvailable());
    carpal::StreamValue<int,int> vv = q.dequeue();
    CHECK(!vv.isItem());
    CHECK(vv.isEof());
    CHECK(vv.eof() == 10);
}

TEST_CASE("SingleProducerSingleConsumeQueue_capacity1", "[queue]") {
    carpal::SingleProducerSingleConsumerQueue<int,int> q(1);
    CHECK(!q.isValueAvailable());
    CHECK(q.isSlotAvailable());
    q.enqueue(carpal::StreamValue<int,int>::makeItem(10));
    CHECK(q.isValueAvailable());
    CHECK(!q.isSlotAvailable());

    int count = 0;
    carpal::StreamValue<int,int> next = carpal::StreamValue<int,int>::makeItem(22);
    auto callback = [&count,&q,&next]() {
        ++count;
        q.enqueue(std::move(next));
    };
    q.setOnSlotAvailableOnceCallback(callback);
    CHECK(count == 0);
    CHECK(q.isValueAvailable());
    CHECK(!q.isSlotAvailable());

    carpal::StreamValue<int,int> v = q.dequeue();
    CHECK(v.isItem());
    CHECK(v.item() == 10);
    CHECK(count == 1);
    CHECK(q.isValueAvailable());
    CHECK(!q.isSlotAvailable());

    v = q.dequeue();
    CHECK(v.isItem());
    CHECK(v.item() == 22);
    CHECK(count == 1);
    CHECK(!q.isValueAvailable());
    CHECK(q.isSlotAvailable());

    next = carpal::StreamValue<int,int>::makeItem(25);
    q.setOnSlotAvailableOnceCallback(callback);
    CHECK(count == 2);
    CHECK(q.isValueAvailable());
    CHECK(!q.isSlotAvailable());

    v = q.dequeue();
    CHECK(v.isItem());
    CHECK(v.item() == 25);
    CHECK(!q.isValueAvailable());
    CHECK(q.isSlotAvailable());
}

