// Copyright Radu Lupsa 2023-2004
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/OneThreadScheduler.h"
#include "carpal/Logger.h"

carpal::OneThreadScheduler::OneThreadScheduler()
    :m_threadId(std::this_thread::get_id())
{
    CARPAL_LOG_DEBUG("Creating OneThreadScheduler @", static_cast<void const*>(this), " for thread ", m_threadId);
}

carpal::OneThreadScheduler::OneThreadScheduler(std::thread::id threadId)
    :m_threadId(threadId)
{
    CARPAL_LOG_DEBUG("Creating OneThreadScheduler @", static_cast<void const*>(this), " for thread ", m_threadId);
}

carpal::OneThreadScheduler::~OneThreadScheduler() {
    CARPAL_LOG_DEBUG("OneThreadScheduler @", static_cast<void const*>(this), " ended");
}

void carpal::OneThreadScheduler::enqueue(std::function<void()> func) {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_tasks.push(std::move(func));
    m_cv.notify_one();
}

bool carpal::OneThreadScheduler::initSwitchThread() const {
    return std::this_thread::get_id() != m_threadId;
}

void carpal::OneThreadScheduler::markRunnable(std::coroutine_handle<void> h, bool /*expect_end_soon*/) {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_runnableHandlers.push(h);
    m_cv.notify_one();
    CARPAL_LOG_DEBUG("Coroutine ", h.address(), " marked runnable");
}

void carpal::OneThreadScheduler::markCompleted(const void* id) {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_finishedWaiters.insert(id);
    CARPAL_LOG_DEBUG("Waiter ", id, " marked runnable on OneThreadScheduler @", static_cast<void const*>(this));
    m_cv.notify_all();
}

void carpal::OneThreadScheduler::waitFor(const void* id) {
    if(std::this_thread::get_id() == m_threadId) {
        CARPAL_LOG_DEBUG("Will run other things until waiter ", id, " is runnable");
        std::unique_lock<std::mutex> lck(m_mtx);
        while (true) {
            auto waiterIter = m_finishedWaiters.find(id);
            if(waiterIter != m_finishedWaiters.end()) {
                m_finishedWaiters.erase(waiterIter);
                CARPAL_LOG_DEBUG("Waiter id ", id, " completed");
                return;
            }
            if(!m_runnableHandlers.empty()) {
                std::coroutine_handle<void> h = m_runnableHandlers.front();
                m_runnableHandlers.pop();
                CARPAL_LOG_DEBUG("Resuming coroutine ", h.address());
                lck.unlock();
                h.resume();
                lck.lock();
            } else if(!m_tasks.empty()) {
                std::function<void()> task = m_tasks.front();
                m_tasks.pop();
                CARPAL_LOG_DEBUG("Executing task");
                lck.unlock();
                task();
                lck.lock();
                CARPAL_LOG_DEBUG("Task finished");
            } else {
                CARPAL_LOG_DEBUG("No other jobs to run now; waiting");
                m_cv.wait(lck);
            }
        }
    } else {
        CARPAL_LOG_DEBUG("Will wait until waiter ", id, " is runnable");
        std::unique_lock<std::mutex> lck(m_mtx);
        while (true) {
            auto waiterIter = m_finishedWaiters.find(id);
            if(waiterIter != m_finishedWaiters.end()) {
                m_finishedWaiters.erase(waiterIter);
                CARPAL_LOG_DEBUG("Waiter id ", id, " completed");
                return;
            }
            m_cv.wait(lck);
        }
    }
}

void const* carpal::OneThreadScheduler::address() const {
    return this;
}

void carpal::OneThreadScheduler::runAllPending() {
    if(std::this_thread::get_id() != m_threadId) {
        CARPAL_LOG_DEBUG("Wrong thread for OneThreadScheduler");
        return;
    }
    CARPAL_LOG_DEBUG("Will run pending jobs");
    std::unique_lock<std::mutex> lck(m_mtx);
    while(true) {
        if(!m_runnableHandlers.empty()) {
            std::coroutine_handle<void> h = m_runnableHandlers.front();
            m_runnableHandlers.pop();
            CARPAL_LOG_DEBUG("Resuming coroutine ", h.address());
            lck.unlock();
            h.resume();
            lck.lock();
        } else if(!m_tasks.empty()) {
            std::function<void()> task = m_tasks.front();
            m_tasks.pop();
            CARPAL_LOG_DEBUG("Executing task");
            lck.unlock();
            task();
            lck.lock();
            CARPAL_LOG_DEBUG("Task finished");
        } else {
            CARPAL_LOG_DEBUG("No other jobs to run now");
            return;
        }
    }
}
