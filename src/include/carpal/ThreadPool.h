// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#pragma once

#include <functional>
#include <deque>
#include <queue>
#include <vector>
#include <thread>
#include <condition_variable>
#include <unordered_set>

#include "carpal/config.h"
#include "Executor.h"
#ifdef ENABLE_COROUTINES
#include "CoroutineScheduler.h"
#endif // ENABLE_COROUTINES

namespace carpal {

class ThreadPool
#ifdef ENABLE_COROUTINES
    : public CoroutineScheduler
#else // ENABLE_COROUTINES
    : public Executor
#endif // not ENABLE_COROUTINES
{
public:
    explicit ThreadPool(unsigned nrThreads);
    ~ThreadPool() override;
    void enqueue(std::function<void()> func) override;

    void close();

#ifdef ENABLE_COROUTINES
    bool initSwitchThread() const override;

    void markRunnable(std::coroutine_handle<void> h) override;
#endif // ENABLE_COROUTINES

    void markCompleted(const void* id) override;

    void waitFor(const void* id) override;

private:
    void threadFunc();

    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::queue<std::function<void()> > m_tasks;
    bool m_isEnding = false;

#ifdef ENABLE_COROUTINES
    std::queue<std::coroutine_handle<void> > m_runnableHandlers;
#endif // ENABLE_COROUTINES

    std::unordered_set<void const*> m_finishedWaiters;

    std::vector<std::thread> m_threads;
};

} // namespace carpal
