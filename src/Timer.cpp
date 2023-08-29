#include "carpal/Timer.h"

#include <assert.h>

namespace carpal {

namespace carpal_private {

class TimerFutureObject : public PromiseFuturePair<bool> {
public:
    explicit TimerFutureObject(std::chrono::system_clock::time_point const& when, AlarmClock* pClock)
        :m_when(when)
        ,m_pClock(pClock)
    {
        // nothing else
    }
private:
    friend AlarmClock;
    friend Timer;
    void trigger() {
        if(!this->isComplete()) {
            this->set(true);
        }
    }
    
    std::chrono::system_clock::time_point m_when;
    AlarmClock* m_pClock;
};    

} // namespace carpal_private

Future<bool> Timer::getFuture() {
    return Future<bool>(std::shared_ptr<PromiseFuturePair<bool> >(m_pFuture));
}

void Timer::cancel() {
    m_pFuture->m_pClock->cancelTimer(m_pFuture);
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
    std::shared_ptr<carpal_private::TimerFutureObject> ret = std::make_shared<carpal_private::TimerFutureObject>(when, this);
    std::unique_lock<std::mutex> lck(m_mtx);
    auto it = m_timers.emplace(ret).first;
    if(it == m_timers.begin()) {
        m_cond.notify_all();
    }
    return Timer(ret);
}

void AlarmClock::cancelTimer(std::shared_ptr<carpal_private::TimerFutureObject> pTimerObject) {
    assert(pTimerObject->m_pClock == this);
    std::unique_lock<std::mutex> lck(m_mtx);
    if(!pTimerObject->isComplete()) {
        pTimerObject->set(false);
    }
    auto it = m_timers.find(pTimerObject);
    if(it == m_timers.begin() || m_nextTimer == pTimerObject) {
        m_cond.notify_all();
    }
    if(it != m_timers.end()) {
        m_timers.erase(it);
    }
    if(m_nextTimer == pTimerObject) {
        m_nextTimer = nullptr;
    }
}

bool AlarmClock::compareTimers(std::shared_ptr<carpal_private::TimerFutureObject> const& p,
    std::shared_ptr<carpal_private::TimerFutureObject> const& q) {
        return ((p->m_when < q->m_when) || (p->m_when == q->m_when && p < q));
}

void AlarmClock::threadFunction() {
    std::unique_lock<std::mutex> lck(m_mtx);
    while(true) {
        if(m_timers.empty()) {
            if(m_closed) return;
            m_cond.wait(lck);
        } else {
            m_nextTimer = *(m_timers.begin());
            auto result = m_cond.wait_until(lck, m_nextTimer->m_when);
            if(result == std::cv_status::timeout) {
                while(!m_timers.empty() && (*m_timers.begin())->m_when <= m_nextTimer->m_when) {
                    (*m_timers.begin())->trigger();
                    m_timers.erase(m_timers.begin());
                }
                m_nextTimer.reset();
            } else {
                m_nextTimer.reset();
            }
        }
    }
}

AlarmClock* alarmClock() {
    static AlarmClock alarmClock;
    return &alarmClock;
}

} // namespace carpal
