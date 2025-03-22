// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/Future.h"
#include <thread>

carpal::FutureWaiter::FutureWaiter() = default;
carpal::FutureWaiter::~FutureWaiter() = default;

void carpal::FutureWaiter::add(Future<void> future) {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_futures.push_front(future);
    std::list<Future<void> >::iterator it = m_futures.begin();
    lck.unlock();
    future.addSynchronousCallback([this,it](){onFutureComplete(it);});
}

void carpal::FutureWaiter::waitAll() {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_cond.wait(lck, [this]()->bool {return m_futures.empty();});
}

void carpal::FutureWaiter::onFutureComplete(std::list<Future<void> >::iterator it) {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_futures.erase(it);
    if(m_futures.empty()) {
        m_cond.notify_all();
    }
}
