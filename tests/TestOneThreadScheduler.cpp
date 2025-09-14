// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/Future.h"
#include "carpal/OneThreadScheduler.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <catch2/catch_test_macros.hpp>


using namespace carpal;

namespace {
    class OneThreadSchedulerWrapper {
    public:
        OneThreadSchedulerWrapper() {
            std::unique_lock<std::mutex> lck(m_mtx);
            m_thread.reset(new std::thread(&OneThreadSchedulerWrapper::threadFunc, this));
            m_scheduler.reset(new OneThreadScheduler(m_thread->get_id()));
            m_cond.notify_all();
        }
        ~OneThreadSchedulerWrapper() {
            std::unique_lock<std::mutex> lck(m_mtx);
            m_closing = true;
            m_cond.notify_all();
            lck.unlock();
            m_thread->join();
        }
        OneThreadScheduler* scheduler() {
            return m_scheduler.get();
        }
        CoroutineSchedulingInfo sameThreadStart() {
            return m_scheduler->sameThreadStart();
        }
        CoroutineSchedulingInfo parallelStart() {
            return m_scheduler->parallelStart();
        }
        std::thread::id threadId() const {
            return m_thread->get_id();
        }
    private:
        void threadFunc() {
            std::unique_lock<std::mutex> lck(m_mtx);
            while(true) {
                if(m_closing) {
                    return;
                }
                if(m_scheduler != nullptr) {
                    lck.unlock();
                    m_scheduler->runAllPending(); // TODO: some race conditions here...
                    lck.lock();
                }
            }
        }
    private:
        std::mutex m_mtx;
        std::condition_variable m_cond;
        bool m_closing = false;
        std::unique_ptr<std::thread> m_thread;
        std::unique_ptr<OneThreadScheduler> m_scheduler;
    };

    Future<int> coroFunc(carpal::CoroutineSchedulingInfo const& schedulingInfo, Future<int> f, std::thread::id expectedId) {
        co_await schedulingInfo;
        CHECK(std::this_thread::get_id() == expectedId);
        int ret = (co_await f) + 1;
        CHECK(std::this_thread::get_id() == expectedId);
        co_return ret;
    }

} // unnamed namespace

TEST_CASE("OneThreadScheduler_simple", "[oneThreadScheduler]") {
    OneThreadSchedulerWrapper scheduler;
    Promise<int> p;
    CHECK(std::this_thread::get_id() != scheduler.threadId());
    auto coro = coroFunc(scheduler.sameThreadStart(), p.future(), scheduler.threadId());
    p.set(20);
    CHECK(coro.get() == 21);
}

TEST_CASE("OneThreadScheduler_coro", "[oneThreadScheduler]") {
    OneThreadSchedulerWrapper scheduler;
    Promise<int> p;
    auto c1 = coroFunc(scheduler.sameThreadStart(), p.future(), scheduler.threadId());
    auto c2 = coroFunc(scheduler.sameThreadStart(), c1, scheduler.threadId());
    auto c3 = coroFunc(scheduler.sameThreadStart(), c1, scheduler.threadId());
    p.set(20);
    CHECK(c1.get() == 21);
    CHECK(c2.get() == 22);
    CHECK(c3.get() == 22);
}
