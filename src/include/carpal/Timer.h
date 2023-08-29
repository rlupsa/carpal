#pragma once

#include "Future.h"

#include <chrono>
#include <set>
#include <memory>
#include <thread>

namespace carpal {

class Timer;
class AlarmClock;

namespace carpal_private {

class TimerFutureObject;    

} // namespace carpal_private

class Timer {
public:
    Timer(std::shared_ptr<carpal_private::TimerFutureObject> pFuture)
        :m_pFuture(pFuture)
        {}
    Future<bool> getFuture();
    void cancel();

private:
    std::shared_ptr<carpal_private::TimerFutureObject> m_pFuture;
};

template<typename T>
class TimedAction {
public:
    Future<T> getFuture() const;
    operator Future<T>() const;
    void cancel();
};

/** @brief An object that can be used for scheduling one-shot or periodic actions
 * */
class AlarmClock {
public:
    AlarmClock();
    ~AlarmClock();

    /** @brief Terminates the alarm clock. Events not triggered yet are canceled
     * */
    void close();
    Timer setTimer(std::chrono::system_clock::time_point when);
    Timer setTimerAfter(std::chrono::system_clock::duration delta);
    
    template<typename Func>
    Future<typename std::invoke_result<Func>::type> setTimedAction();

    void cancelTimer(std::shared_ptr<carpal_private::TimerFutureObject> pTimerObject);

private:
    static bool compareTimers(std::shared_ptr<carpal_private::TimerFutureObject> const& p,
        std::shared_ptr<carpal_private::TimerFutureObject> const& q);
    void threadFunction();

    std::mutex m_mtx;
    std::condition_variable m_cond;
    std::set<std::shared_ptr<carpal_private::TimerFutureObject>, decltype(&AlarmClock::compareTimers)> m_timers;
    std::shared_ptr<carpal_private::TimerFutureObject> m_nextTimer;
    bool m_closed = false;
    std::thread m_thread;
};

AlarmClock* alarmClock();

} // namespace carpal
