// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/CoroutineScheduler.h"

carpal::CoroutineScheduler::CoroutineScheduler() = default;

carpal::CoroutineScheduler::~CoroutineScheduler() = default;

void carpal::CoroutineScheduler::markRunnable(std::coroutine_handle<void> h) {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_runnableHandlers.push(h);
    m_cv.notify_one();
}

void carpal::CoroutineScheduler::markThreadRunnable(std::thread::id tid) {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_runnableThreads.insert(tid);
    m_cv.notify_all();
}

std::coroutine_handle<void> carpal::CoroutineScheduler::schedule() {
    auto id = std::this_thread::get_id();
    std::unique_lock<std::mutex> lck(m_mtx);
    while (true) {
        auto threadIter = m_runnableThreads.find(id);
        if(threadIter != m_runnableThreads.end()) {
            m_runnableThreads.erase(threadIter);
            return nullptr;
        }
        if(!m_runnableHandlers.empty()) {
            std::coroutine_handle<void> h = m_runnableHandlers.front();
            m_runnableHandlers.pop();
            return h;
        }
        m_cv.wait(lck);
    }
}

carpal::CoroutineScheduler* carpal::defaultCoroutineScheduler() {
    static CoroutineScheduler scheduler;
    return &scheduler;
}
