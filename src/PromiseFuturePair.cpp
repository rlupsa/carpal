// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/PromiseFuturePair.h"
#include <thread>

carpal::PromiseFuturePairBase::~PromiseFuturePairBase() {
}

void carpal::PromiseFuturePairBase::notify(State state) {
    std::unique_lock<std::mutex> lck(m_mtx);
    std::function<void()> continuations = std::move(m_continuations);
    m_cv.notify_all();
    m_state = state;
    lck.unlock();
    if(continuations != nullptr) {
        continuations();
    }
}

void carpal::PromiseFuturePairBase::addSynchronousCallback(std::function<void()> func) {
    std::unique_lock<std::mutex> lck(m_mtx);
    if (m_state != State::not_completed) {
        lck.unlock();
        func();
    } else if(m_continuations == nullptr) {
        m_continuations = std::move(func);
    } else {
        m_continuations = [oldCont=std::move(m_continuations),f=std::move(func)](){
            oldCont();
            f();
        };
    }
}

void carpal::PromiseFuturePairBase::dispose() {
    delete this;
}
