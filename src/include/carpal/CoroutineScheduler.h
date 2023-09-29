// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#pragma once

#include <coroutine>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_set>

namespace carpal {

/** @brief Scheduler for coroutines
 * */
class CoroutineScheduler {
public:
    CoroutineScheduler();
    ~CoroutineScheduler();

    /** @brief Marks the specified coroutine handler runnable. This means it will be returned, at some later time, by a @c schedule() call
     * */
    void markRunnable(std::coroutine_handle<void> h);

    /** @brief Marks the specified thread runnable. This means it will return, at some later time, by a @c schedule() call
     * */
    void markThreadRunnable(std::thread::id tid);

    /** @brief Returns a runnable coroutine (specified by a @c markRunnable() call), or @c nullptr if this thread is specified as runnable
     * by a @c markRunnable() call.
     * */
    std::coroutine_handle<void> schedule();

private:
    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::queue<std::coroutine_handle<void> > m_runnableHandlers;
    std::unordered_set<std::thread::id> m_runnableThreads;

};

CoroutineScheduler* defaultCoroutineScheduler();

} // namespace carpal
