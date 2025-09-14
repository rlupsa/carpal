// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#pragma once

#include "carpal/Future.h"
#include "carpal/StreamSource.h"

#include <chrono>
#include <set>
#include <memory>
#include <thread>

namespace carpal {

class AlarmClock;
class Timer;
class AlarmClock;

namespace carpal_private {

class BaseTimer;
class TimerFutureObject;
class PeriodicTimerQueueObject;

} // namespace carpal_private

class Timer {
public:
    Timer(IntrusiveSharedPtr<carpal_private::TimerFutureObject> pFuture);
    ~Timer();
    Future<bool> getFuture();
    void cancel();

private:
    IntrusiveSharedPtr<carpal_private::TimerFutureObject> m_pFuture;
};

class PeriodicTimer {
public:
    PeriodicTimer(IntrusiveSharedPtr<carpal_private::PeriodicTimerQueueObject> queue);
    ~PeriodicTimer();
    StreamSource<std::chrono::system_clock::time_point> getStream();
    void cancel();

private:
    IntrusiveSharedPtr<carpal_private::PeriodicTimerQueueObject> m_queue;
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

    PeriodicTimer setPeriodicTimer(std::chrono::system_clock::duration period);
    PeriodicTimer setPeriodicTimerStartAt(std::chrono::system_clock::duration period, std::chrono::system_clock::time_point when);
    PeriodicTimer setPeriodicTimerStartAfter(std::chrono::system_clock::duration period, std::chrono::system_clock::duration delta);

private:
    friend class Timer;
    friend class PeriodicTimer;
    friend class carpal_private::TimerFutureObject;
    friend class carpal_private::PeriodicTimerQueueObject;

    void cancelTimerObject(carpal_private::BaseTimer* pTimerObject);
    void addTimerObject(carpal_private::BaseTimer* pTimerObject);
    bool removeTimerObject(carpal_private::BaseTimer* pTimerObject);
    static bool compareTimers(carpal_private::BaseTimer* p, carpal_private::BaseTimer* q);
    void threadFunction();

    std::mutex m_mtx;
    std::condition_variable m_cond;
    std::set<carpal_private::BaseTimer*, decltype(&AlarmClock::compareTimers)> m_timers;
    bool m_closed = false;
    std::thread m_thread;
};

AlarmClock* alarmClock();

} // namespace carpal
