// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/Logger.h"
#include "carpal/Timer.h"

#include <assert.h>

namespace carpal {

namespace carpal_private {

class BaseTimer {
public:
    BaseTimer(std::chrono::system_clock::time_point const& when, AlarmClock* pClock)
        :m_when(when)
        ,m_pClock(pClock)
    {
        // nothing else
    }
    /** @brief Triggers the timer. Assumes it is already removed from the clock. Returns true if the timer is periodic and needs to be re-inserted */
    virtual bool trigger() = 0;
    /** @brief Cancels the timer. Assumes it is already removed from the clock */
    virtual void cancel() = 0;
    /** @brief Decrements the reference counter */
    virtual void unlinkFromClock() = 0;

    AlarmClock* clock() {
        return m_pClock;
    }
protected:
    friend class ::carpal::AlarmClock;
    std::chrono::system_clock::time_point m_when;
    AlarmClock* m_pClock;
};

class TimerFutureObject : public PromiseFuturePair<bool>, public BaseTimer {
public:
    TimerFutureObject(std::chrono::system_clock::time_point const& when, AlarmClock* pClock)
        :BaseTimer(when, pClock)
    {
        this->addRef();
    }
    bool trigger() override {
        CARPAL_LOG_DEBUG("Triggering timer @", static_cast<void const*>(this));
        if(!this->isComplete()) {
            this->set(true);
        } else {
            CARPAL_LOG_ERROR("Timer @", static_cast<void const*>(this), " already triggered");
        }
        return false;
    }
    void cancel() override {
        CARPAL_LOG_DEBUG("Cancelling timer @", static_cast<void const*>(this));
        if(!this->isComplete()) {
            this->set(false);
        } else {
            CARPAL_LOG_ERROR("Timer @", static_cast<void const*>(this), " already cancelled");
        }
    }
    void unlinkFromClock() override {
        CARPAL_LOG_DEBUG("Unlinking timer @", static_cast<void const*>(this));
        this->removeRef();
    }
};

} // namespace carpal_private

Timer::Timer(IntrusiveSharedPtr<carpal_private::TimerFutureObject> pFuture)
    :m_pFuture(pFuture)
{}

Timer::~Timer() = default;

Future<bool> Timer::getFuture() {
    return Future<bool>(m_pFuture);
}

void Timer::cancel() {
    m_pFuture->clock()->cancelTimerObject(m_pFuture.ptr());
}

AlarmClock::AlarmClock()
    :m_timers(&AlarmClock::compareTimers)
    ,m_thread(&AlarmClock::threadFunction, this)
{
    // empty
}

AlarmClock::~AlarmClock() {
    close();
    m_thread.join();
}

void AlarmClock::close() {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_closed = true;
    m_cond.notify_all();
}

Timer AlarmClock::setTimer(std::chrono::system_clock::time_point when) {
    carpal_private::TimerFutureObject* pTimerObject = new carpal_private::TimerFutureObject(when, this);
    CARPAL_LOG_DEBUG("Created timer @", static_cast<void const*>(pTimerObject), " to trigger at ", when);
    std::unique_lock<std::mutex> lck(m_mtx);
    addTimerObject(pTimerObject);
    return Timer(IntrusiveSharedPtr<carpal_private::TimerFutureObject>(pTimerObject));
}

Timer AlarmClock::setTimerAfter(std::chrono::system_clock::duration delta) {
    return setTimer(std::chrono::system_clock::now() + delta);
}

void AlarmClock::cancelTimerObject(carpal_private::BaseTimer* pTimerObject) {
    assert(pTimerObject->m_pClock == this);
    std::unique_lock<std::mutex> lck(m_mtx);
    auto result = removeTimerObject(pTimerObject);
    if(result == 1) {
        CARPAL_LOG_DEBUG("Cancelling timer @", static_cast<void const*>(pTimerObject));
        pTimerObject->cancel();
    }
}

void AlarmClock::addTimerObject(carpal_private::BaseTimer* pTimerObject) {
    assert(pTimerObject->m_pClock == this);
    CARPAL_LOG_DEBUG("Adding timer @", static_cast<void const*>(pTimerObject), " to AlarmClock");
    auto result = m_timers.insert(pTimerObject);
    if(result.first == m_timers.begin()) {
        m_cond.notify_all();
    }
}

bool AlarmClock::removeTimerObject(carpal_private::BaseTimer* pTimerObject) {
    assert(pTimerObject->m_pClock == this);
    CARPAL_LOG_DEBUG("Removing timer @", static_cast<void const*>(pTimerObject), " from AlarmClock");
    auto it = m_timers.find(pTimerObject);
    if(it == m_timers.begin()) {
        m_cond.notify_all();
    }
    if(it != m_timers.end()) {
        m_timers.erase(it);
        return true;
    } else {
        return false;
        CARPAL_LOG_DEBUG("Timer @", static_cast<void const*>(pTimerObject), " already removed");
    }
}

bool AlarmClock::compareTimers(carpal_private::BaseTimer* p, carpal_private::BaseTimer* q) {
    return ((p->m_when < q->m_when) || (p->m_when == q->m_when && p < q));
}

void AlarmClock::threadFunction() {
    CARPAL_LOG_DEBUG("AlarmClock thread created");
    std::unique_lock<std::mutex> lck(m_mtx);
    while(true) {
        if(m_timers.empty()) {
            if(m_closed) {
                CARPAL_LOG_DEBUG("AlarmClock thread exiting");
                return;
            }
            CARPAL_LOG_DEBUG("AlarmClock thread waiting - no timers scheduled");
            m_cond.wait(lck);
        } else {
            std::chrono::system_clock::time_point const when = (*m_timers.begin())->m_when;
            CARPAL_LOG_DEBUG("AlarmClock thread waiting until ", when);
            auto result = m_cond.wait_until(lck, when);
            if(result == std::cv_status::timeout) {
                CARPAL_LOG_DEBUG("AlarmClock thread processing triggered timer(s)");
                while(!m_timers.empty() && (*m_timers.begin())->m_when <= when) {
                    carpal_private::BaseTimer* pTimer = (*m_timers.begin());
                    m_timers.erase(m_timers.begin());
                    if(pTimer->trigger()) {
                        addTimerObject(pTimer);
                    } else {
                        pTimer->unlinkFromClock();
                    }
                }
            }
        }
    }
}

AlarmClock* alarmClock() {
    static AlarmClock alarmClock;
    return &alarmClock;
}
} // namespace carpal
