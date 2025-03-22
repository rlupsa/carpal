// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/ThreadPool.h"
#include "carpal/Logger.h"

carpal::ThreadPool::ThreadPool(unsigned nrThreads) {
    CARPAL_LOG_DEBUG("Creating thread pool @", static_cast<void const*>(this), " with ", nrThreads, " threads");
    m_threads.reserve(nrThreads);
    for(unsigned i=0 ; i<nrThreads ; ++i) {
        m_threads.emplace_back(&ThreadPool::threadFunc, this);
    }
}

carpal::ThreadPool::~ThreadPool() {
    close();
    for(std::thread& t : m_threads) {
        t.join();
    }
    CARPAL_LOG_DEBUG("Thread pool @", static_cast<void const*>(this), " ended");
}
void carpal::ThreadPool::close() {
     std::unique_lock<std::mutex> lck(m_mtx);
     m_isEnding = true;
     m_cv.notify_all();
     CARPAL_LOG_DEBUG("Closing thread pool @", static_cast<void const*>(this));
}

void carpal::ThreadPool::enqueue(std::function<void()> func) {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_tasks.push(std::move(func));
    m_cv.notify_one();
}

#ifdef ENABLE_COROUTINES
bool carpal::ThreadPool::initSwitchThread() const {
    return false;
}

void carpal::ThreadPool::markRunnable(std::coroutine_handle<void> h) {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_runnableHandlers.push(h);
    m_cv.notify_one();
    CARPAL_LOG_DEBUG("Coroutine ", h.address(), " marked runnable on thread pool @", static_cast<void const*>(this));
}
#endif // ENABLE_COROUTINES

void carpal::ThreadPool::markCompleted(const void* id) {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_finishedWaiters.insert(id);
    CARPAL_LOG_DEBUG("Waiter ", id, " marked runnable");
    m_cv.notify_all();
}

void carpal::ThreadPool::waitFor(const void* id) {
    std::unique_lock<std::mutex> lck(m_mtx);
    CARPAL_LOG_DEBUG("ThreadPool::waitFor(): Will run other things until waiter ", id, " is runnable");
    while (true) {
        auto waiterIter = m_finishedWaiters.find(id);
        if(waiterIter != m_finishedWaiters.end()) {
            m_finishedWaiters.erase(waiterIter);
            CARPAL_LOG_DEBUG("ThreadPool::waitFor(): Waiter id ", id, " completed");
            return;
        }
#ifdef ENABLE_COROUTINES
        if(!m_runnableHandlers.empty()) {
            std::coroutine_handle<void> h = m_runnableHandlers.front();
            m_runnableHandlers.pop();
            CARPAL_LOG_DEBUG("ThreadPool::waitFor(): Resuming coroutine ", h.address());
            lck.unlock();
            h.resume();
            lck.lock();
        } else
#endif // ENABLE_COROUTINES
        if(!m_tasks.empty()) {
            std::function<void()> func = std::move(m_tasks.front());
            m_tasks.pop();
            lck.unlock();
            CARPAL_LOG_DEBUG("ThreadPool::waitFor(): Executing work item");
            func();
            lck.lock();
        } else {
            CARPAL_LOG_DEBUG("ThreadPool::waitFor(): Waiting for work");
            m_cv.wait(lck);
        }
    }
}

void carpal::ThreadPool::threadFunc()
{
    CARPAL_LOG_DEBUG("ThreadPool::threadFunc(): Starting thread on thread pool");
    std::unique_lock<std::mutex> lck(m_mtx);
    while(true) {
#ifdef ENABLE_COROUTINES
        if(!m_runnableHandlers.empty()) {
            std::coroutine_handle<void> h = m_runnableHandlers.front();
            m_runnableHandlers.pop();
            lck.unlock();
            CARPAL_LOG_DEBUG("ThreadPool::threadFunc(): Resuming coroutine ", h.address());
            h.resume();
            lck.lock();
        } else
#endif // ENABLE_COROUTINES
        if(!m_tasks.empty()) {
            std::function<void()> func = std::move(m_tasks.front());
            m_tasks.pop();
            lck.unlock();
            CARPAL_LOG_DEBUG("ThreadPool::threadFunc(): Executing work item");
            func();
            lck.lock();
        } else if(m_isEnding) {
            CARPAL_LOG_DEBUG("ThreadPool::threadFunc(): Ending thread on thread pool");
            return;
        } else {
            CARPAL_LOG_DEBUG("ThreadPool::threadFunc(): Waiting for work");
            m_cv.wait(lck);
        }
    }
}

namespace {
carpal::ThreadPool* defaultThreadPool() {
    static carpal::ThreadPool threadPool(std::thread::hardware_concurrency() + 1);
    return &threadPool;
}
} // unnamed namespace

carpal::Executor* carpal::defaultExecutor() {
    return defaultThreadPool();
}

#ifdef ENABLE_COROUTINES
carpal::CoroutineScheduler* carpal::defaultCoroutineScheduler() {
    return defaultThreadPool();
}
#endif // ENABLE_COROUTINES
