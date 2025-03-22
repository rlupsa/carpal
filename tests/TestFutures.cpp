// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/Future.h"
#include "carpal/Timer.h"
#include "carpal/ThreadPool.h"

#include <catch2/catch_test_macros.hpp>
#include <stdio.h>

#include "TestHelper.h"

using namespace carpal;

TEST_CASE("Future_void_completed", "[future]") {
    int cont = 0;
    Future<void> f = completedFuture();
    CHECK(f.isComplete());
    f.addSynchronousCallback([&cont]() {cont = 555; });
    CHECK(cont == 555);
}

TEST_CASE("Future_completed", "[future]") {
    int cont = 0;
    Future<int> f = completedFuture(42);
    CHECK(f.isComplete());
    CHECK(f.get() == 42);
    f.addSynchronousCallback([&cont,f]() {
        cont = 2 * f.get();
    });
    CHECK(f.get() == 42);
    CHECK(cont == 84);
}

TEST_CASE("Future_void_not_completed", "[future]") {
    Promise<void> p;
    Future<void> f = p.future();
    int cont = 0;
    f.addSynchronousCallback([&cont, f]() {
        cont = 11;
        });
    CHECK(!f.isComplete());
    CHECK(cont == 0);
    p.set();
    CHECK(f.isComplete());
    CHECK(cont == 11);
}

TEST_CASE("Future_not_completed", "[future]") {
    Promise<int> p;
    Future<int> f = p.future();
    int cont = 0;
    f.addSynchronousCallback([&cont, f]() {
        cont = f.get() + 1;
    });
    CHECK(!f.isComplete());
    CHECK(cont == 0);
    p.set(10);
    CHECK(f.isComplete());
    CHECK(f.get() == 10);
    CHECK(cont == 11);
}

TEST_CASE("ThreadPool_simple", "[futures]") {
    ThreadPool tp(32);
    bool isReady = false;
    int val = 0;
    std::mutex mtx;
    std::condition_variable cv;
    tp.enqueue([&](){
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::unique_lock lck(mtx);
        cv.notify_one();
        val = 42;
        isReady = true;
    });

    {
        std::unique_lock lck(mtx);
        while(!isReady) cv.wait(lck);
    }
    CHECK(42 == val);
}

TEST_CASE("Future_start_immediately", "[futures]") {
    ThreadPool tp(32);
    Future<int> f = runAsync(&tp, [](){
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 42;
    });
    CHECK(42 == f.get());
    CHECK(f.isComplete());
}

TEST_CASE("Future_void_start_immediately", "[futures]") {
    ThreadPool tp(32);
    int cont = 0;
    Future<void> f = runAsync(&tp, [&cont]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        cont = 42;
    });
    f.wait();
    CHECK(42 == cont);
    CHECK(f.isComplete());
}

TEST_CASE("Futures_continuations", "[futures]") {
    ThreadPool tp(32);
    Promise<int> pf;
    auto func = [](Future<int> fv)->int {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return fv.get() + 1;
    };
    Future<int> f2 = whenAllFromFutures(&tp, func, pf.future());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!f2.isComplete());
    pf.set(10);
    CHECK(11 == f2.get());
    CHECK(f2.isComplete());
}

TEST_CASE("Futures_continuation_then", "[futures]") {
    ThreadPool tp(32);
    Promise<int> pf;
    int var = 0;
    Future<int> f2 = pf.future()
        .then(&tp, [](int a)->int {std::this_thread::sleep_for(std::chrono::milliseconds(10)); return a + 1; })
        .then(&tp, [&var](int a)->void {std::this_thread::sleep_for(std::chrono::milliseconds(10)); var = a + 1; })
        .then(&tp, [&var]()->int {std::this_thread::sleep_for(std::chrono::milliseconds(10)); return var + 1; });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!f2.isComplete());
    pf.set(10);
    CHECK(13 == f2.get());
    CHECK(12 == var);
    CHECK(f2.isComplete());
}

TEST_CASE("Futures_continuation_then_default_executor", "[futures]") {
    Promise<int> pf;
    int var = 0;
    Future<int> f2 = pf.future()
        .then([](int a)->int {delay(10); return a + 1; })
        .then([&var](int a)->void {delay(10); var = a + 1; })
        .then([&var]()->int {delay(10); return var + 1; });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!f2.isComplete());
    pf.set(10);
    CHECK(13 == f2.get());
    CHECK(12 == var);
    CHECK(f2.isComplete());
}

TEST_CASE("Futures_continuation_then_async", "[futures]") {
    ThreadPool tp(32);
    Promise<int> pf;
    int var = 0;
    Future<int> f2 = pf.future()
        .thenAsync(&tp, [](int a)->Future<int> {return completeLater(a+1, 20); })
        .thenAsync(&tp, [&var](int a)->Future<void> {return executeLaterVoid([&var,a](){var = a + 1; });})
        .thenAsync(&tp, [&var]()->Future<int> {return executeLater([&var]()->int{ return var + 1; });});
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!f2.isComplete());
    pf.set(10);
    CHECK(13 == f2.get());
    CHECK(12 == var);
    CHECK(f2.isComplete());
}

TEST_CASE("Futures_continuations_void", "[futures]") {
    ThreadPool tp(32);
    Promise<int> pi;
    Promise<void> pv;
    int cont1 = 0;
    int cont2 = 0;
    Future<int> fv2i = whenAllFromFutures(&tp,
        [](Future<void> ) {return 42; },
        pv.future());
    Future<void> fv2v = whenAllFromFutures(&tp,
        [&cont1](Future<void>) {cont1 = 82; },
        pv.future());
    Future<void> fi2v = whenAllFromFutures(&tp, [&cont2](Future<int> f) {cont2 = f.get(); }, pi.future());

    CHECK(0 == cont1);
    pv.set();
    CHECK(42 == fv2i.get());
    fv2v.wait();
    CHECK(82 == cont1);

    CHECK(0 == cont2);
    pi.set(5);
    fi2v.wait();
    CHECK(5 == cont2);
}

TEST_CASE("Futures_continuation_loop", "[futures]") {
    auto cond = [](int x)->bool {return x<10;};
    auto body = [](int x)->Future<int> {
        return completeLater(x+1);
    };
    Promise<int> pf;
    Future<int> f2 = pf.future()
        .thenAsyncLoop(std::move(cond), std::move(body));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!f2.isComplete());
    pf.set(0);
    CHECK(10 == f2.get());
    CHECK(f2.isComplete());
}

TEST_CASE("Futures_continuation_loop_void", "[futures]") {
    int val = 0;
    auto cond = [&val]()->bool {return val<10;};
    auto body = [&val]()->Future<void> {
        ++val;
        return completeLaterVoid();
    };
    Promise<void> pf;
    Future<void> f2 = pf.future()
        .thenAsyncLoop(std::move(cond), std::move(body));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!f2.isComplete());
    pf.set();
    f2.wait();
    CHECK(10 == val);
    CHECK(f2.isComplete());
}

TEST_CASE("Futures_continuation_catch", "[futures]") {
    Promise<int> pf;
    Future<int> f2 = pf.future()
        .then([](int a)->int {throw a+1; })
        .thenCatch<int>([](int a)->int {return a + 1; });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!f2.isComplete());
    pf.set(10);
    CHECK(12 == f2.get());
    CHECK(f2.isComplete());
}

TEST_CASE("Futures_continuation_catch_async", "[futures]") {
    Promise<int> pf;
    Future<int> f2 = pf.future()
        .then([](int a)->int {throw a+1; })
        .thenCatchAsync<int>(defaultExecutor(), [](int a)->Future<int> { return completeLater(a + 1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!f2.isComplete());
    pf.set(10);
    CHECK(12 == f2.get());
    CHECK(f2.isComplete());
}

TEST_CASE("Futures_continuation_catch_async_not_throw", "[futures]") {
    Promise<int> pf;
    Future<int> f2 = pf.future()
        .then([](int a)->int {return a-1; })
        .thenCatchAsync<int>(defaultExecutor(), [](int a)->Future<int> { return completeLater(a + 1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!f2.isComplete());
    pf.set(10);
    CHECK(9 == f2.get());
    CHECK(f2.isComplete());
}

TEST_CASE("Futures_continuation_catch_other", "[futures]") {
    Promise<int> pf;
    Future<int> f2 = pf.future()
        .then([](int a)->int {throw a+1; })
        .thenCatchAsync<std::string>(defaultExecutor(), [](std::string a)->Future<int> { return completeLater(int(a.size() + 1)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!f2.isComplete());
    pf.set(10);
    f2.wait();
    CHECK(f2.isComplete());
    CHECK(f2.isException());
}

TEST_CASE("Futures_continuation_catch_async_all", "[futures]") {
    Promise<int> pf;
    Future<int> f2 = pf.future()
        .then([](int a)->int {throw a+1; })
        .thenCatchAllAsync(defaultExecutor(), [](std::exception_ptr p)->Future<int> {
                try {
                    std::rethrow_exception(p);
                } catch(int a) {
                    return completeLater(a + 1);
                }
            });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!f2.isComplete());
    pf.set(10);
    CHECK(12 == f2.get());
    CHECK(f2.isComplete());
}

TEST_CASE("Futures_continuation_catch_async_all_void", "[futures]") {
    std::atomic_int val(0);
    Promise<void> pf;
    Future<void> f2 = pf.future()
        .then([&val]()->void {throw 10; })
        .thenCatchAsync<int>([&val](int a)->Future<void> {
                return executeLaterVoid([&val,a](){val.store(a);});
            });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!f2.isComplete());
    pf.set();
    f2.wait();
    CHECK(val.load() == 10);
    CHECK(f2.isComplete());
}

TEST_CASE("Futures_continuations_multiple", "[futures]") {
    ThreadPool tp(32);
    Promise<int> pi;
    Promise<void> pv;
    Promise<int> pi2;
    int cont = 0;
    Future<int> rez = whenAllFromFutures(&tp,
        [&cont](Future<int> f1, Future<void> f2, Future<int> f3) {
            cont = f1.get()+f3.get();
            return cont;
        },
        pi.future(), pv.future(), pi2.future()
    );

    CHECK(0 == cont);
    pv.set();
    pi.set(20);
    CHECK(0 == cont);
    pi2.set(5);
    CHECK(25 == rez.get());
    CHECK(25 == cont);
}

TEST_CASE("Futures_continuations_multiple_direct", "[futures]") {
    ThreadPool tp(32);
    Promise<int> pi;
    Promise<int> pi2;
    Future<int> rez = whenAll(&tp,
        [](int a, int b) -> int { return a + b + 1;}, pi.future(), pi2.future()
            );

    pi.set(20);
    pi2.set(5);
    CHECK(26 == rez.get());
}

TEST_CASE("Futures_continuations_multiple_move", "[futures]") {
    ThreadPool tp(32);
    Promise<NonCopyableInt> pi;
    Promise<NonCopyableInt> pi2;
    Future<int> rez = whenAll(&tp,
        [](NonCopyableInt const& a, NonCopyableInt& b) -> int {
            NonCopyableInt c = std::move(b);
            return a.val() + c.val() + 1;
        },
        pi.future(),
        pi2.future()
    );

    pi.set(NonCopyableInt(20));
    pi2.set(NonCopyableInt(5));
    CHECK(26 == rez.get());
    CHECK(20 == pi.future().get().val());
    CHECK(-1 == pi2.future().get().val());
}

TEST_CASE("Futures_exception_simple_get_exception", "[futures]") {
    ThreadPool tp(32);
    Promise<int> pi;
    Future<int> rez = whenAll(&tp,
        [](int a) -> int { throw a+1;},
        pi.future());

    pi.set(42);
    rez.wait();
    CHECK(rez.isException());
    std::exception_ptr pex = rez.getException();
    CHECK(pex != nullptr);
}

TEST_CASE("Futures_exception_simple_get", "[futures]") {
    ThreadPool tp(32);
    Promise<int> pi;
    Future<int> rez = whenAll(&tp,
        [](int a) -> int { throw a+1;},
        pi.future());

    pi.set(42);
    try {
        rez.get();
        CHECK(false);
    } catch(int v) {
        CHECK(v == 43);
    }
}

TEST_CASE("Futures_exception_propagate", "[futures]") {
    ThreadPool tp(32);
    Promise<int> pi;
    Future<int> rez = whenAll(&tp,
        [](int a) -> int { throw a+1;},
        pi.future());
    Future<int> rez2 = whenAll(&tp,
        [](int a) -> int { return a+1;},
        rez);

    pi.set(42);
    try {
        rez2.get();
        CHECK(false);
    } catch(int v) {
        CHECK(v == 43);
    }
}

TEST_CASE("Futures_misc", "[futures]") {
    printf("Sizeof pointer=%lu\n", sizeof(void*));
    printf("Sizeof function=%lu\n", sizeof(std::function<void()>));
    printf("Sizeof shared_ptr=%lu\n", sizeof(std::shared_ptr<int>));
    printf("Sizeof exception_ptr=%lu\n", sizeof(std::exception_ptr));
    printf("Sizeof mutex=%lu\n", sizeof(std::mutex));
    printf("Sizeof condition_variable=%lu\n", sizeof(std::condition_variable));
    printf("Sizeof PromiseFuturePairBase=%lu\n", sizeof(PromiseFuturePairBase));
//    std::optional<void> v;
//    CHECK(!v.has_value());
}

TEST_CASE("Futures_loop", "[futures]") {
    ThreadPool tp(32);
    //Future<int> start = completedFuture(42);
    Future<int> res = executeAsyncLoop(&tp, 
        [](int v)->bool{return v<52;},
        [&tp](int v)->Future<int> {
            return runAsync(&tp, [v](){
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                return v+1;
            });
        },
        42);
    CHECK(res.get() == 52);
}

TEST_CASE("Futures_waiter", "[futures]") {
    FutureWaiter waiter;
    
    Promise<int> p1;
    waiter.add(p1.future());
    Promise<int> p2;
    waiter.add(p2.future());
    waiter.add(completedFuture());
    p2.set(1);
    Promise<int> p3;
    waiter.add(p3.future());

    std::atomic_bool b(false);
    std::thread t([&b, &waiter]() {
        waiter.waitAll();
        b.store(true);
    });

    delay(10);
    CHECK(b.load() == false);
    p1.set(2);
    p3.set(3);
    t.join();
    CHECK(b.load() == true);
}
