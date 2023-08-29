#include "carpal/ThreadPool.h"

#include <assert.h>

carpal::ThreadPool::ThreadPool(unsigned nrThreads) {
    m_threads.reserve(nrThreads);
    for(unsigned i=0 ; i<nrThreads ; ++i) {
        m_threads.emplace_back(&ThreadPool::threadFunction, this);
    }
}

carpal::ThreadPool::~ThreadPool() {
    close();
    for(std::thread& t : m_threads) {
        t.join();
    }
}

void carpal::ThreadPool::enqueue(std::function<void()> func) {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_tasks.push_back(std::move(func));
    m_cv.notify_one();
}

void carpal::ThreadPool::close() {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_isClosed = true;
    m_cv.notify_all();
}

void carpal::ThreadPool::threadFunction() {
    std::unique_lock<std::mutex> lck(m_mtx);
    while(true) {
        if(!m_tasks.empty()) {
            std::function<void()> func = std::move(m_tasks.front());
            m_tasks.pop_front();
            lck.unlock();
            try {
                func();
            } catch (...) {
                assert(false);
            }
            lck.lock();
        } else if(m_isClosed) {
            return;
        } else {
            m_cv.wait(lck);
        }
    }
}
