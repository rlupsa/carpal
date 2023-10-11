// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#pragma once

#include <functional>
#include <deque>
#include <vector>
#include <thread>
#include <condition_variable>

#include "Executor.h"

namespace carpal {

class ThreadPool : public Executor {
public:
    explicit ThreadPool(unsigned nrThreads);
    ~ThreadPool() override;
    void enqueue(std::function<void()> func) override;

    void close();

private:
    void threadFunction();

    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::deque<std::function<void()> > m_tasks;
    bool m_isClosed = false;

    std::vector<std::thread> m_threads;
};

} // namespace carpal
