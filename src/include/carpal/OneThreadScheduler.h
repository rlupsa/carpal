// Copyright Radu Lupsa 2023-2024
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#pragma once

#include "CoroutineScheduler.h"

#include <thread>

namespace carpal {

/** @brief Scheduler for coroutines
 * */
class OneThreadScheduler : public CoroutineScheduler {
public:
    OneThreadScheduler();
    OneThreadScheduler(std::thread::id threadId);

    ~OneThreadScheduler() override;

    void enqueue(std::function<void()> func) override;

    bool initSwitchThread() const override;

    void markRunnable(std::coroutine_handle<void> h, bool expect_end_soon) override;

    void markCompleted(const void* id) override;

    void waitFor(const void* id) override;
    
    CoroutineSchedulingInfo sameThreadStart() {
        return CoroutineSchedulingInfo(this, CoroutineStart::sameThread);
    }
    CoroutineSchedulingInfo parallelStart() {
        return CoroutineSchedulingInfo(this, CoroutineStart::parallel);
    }

    void runAllPending();

    void const* address() const override;

private:
    std::thread::id const m_threadId;

    std::mutex m_mtx;
    std::condition_variable m_cv;
    bool m_isEnding = false;

    std::queue<std::function<void()> > m_tasks;
    std::queue<std::coroutine_handle<void> > m_runnableHandlers;
    std::unordered_set<void const*> m_finishedWaiters;
};

} // namespace carpal
