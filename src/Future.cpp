#include "carpal/Future.h"
#include "carpal/ThreadPool.h"
#include <thread>

carpal::Executor* carpal::defaultExecutor() {
    static ThreadPool threadPool(std::thread::hardware_concurrency() + 1);
    return &threadPool;
}

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
