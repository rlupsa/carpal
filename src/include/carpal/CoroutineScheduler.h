// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#pragma once

#include "carpal/Executor.h"
#include "carpal/Logger.h"

#include <coroutine>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_set>
#include <vector>

namespace carpal {

/** @brief Scheduler for coroutines
 * */
class CoroutineScheduler : public Executor {
protected:
    virtual ~CoroutineScheduler() {}

public:
    /** @brief To be called from the initial_suspend(). Returns true if the coroutine must switch to another thread, and false if it should continue on the current thread.
     * */
    virtual bool initSwitchThread() const = 0;

    /** @brief Marks the specified coroutine handler runnable. This function is to be called from callbacks set by an awaiter, when the awaited condition is ready
     * Calling this function means the coroutine itself can be scheduled. However, depending on the scheduling policy,
     * the coroutine will run as soon as there is an available thread, or when someone calls get() or some other such function.
     * */
    virtual void markRunnable(std::coroutine_handle<void> h, bool expect_end_soon) = 0;

    /** @brief Address of the scheduler, for logging purposes
     * */
    virtual void const* address() const = 0;
};

enum class CoroutineStart {
    sameThread,
    parallel
};

class CoroutineSchedulingInfo {
public:
    CoroutineSchedulingInfo(CoroutineScheduler* pScheduler, CoroutineStart startInfo)
        :m_pScheduler(pScheduler),
        m_startInfo(startInfo)
        {}
    CoroutineSchedulingInfo(CoroutineSchedulingInfo&& src) = default;
    CoroutineSchedulingInfo(CoroutineSchedulingInfo const& src)= delete;
    CoroutineSchedulingInfo& operator=(CoroutineSchedulingInfo&& src) = default;
    CoroutineSchedulingInfo& operator=(CoroutineSchedulingInfo const& src)= delete;

    CoroutineSchedulingInfo sameThreadStart() const {
        return CoroutineSchedulingInfo(m_pScheduler, CoroutineStart::sameThread);
    }
    CoroutineSchedulingInfo parallelStart() const {
        return CoroutineSchedulingInfo(m_pScheduler, CoroutineStart::parallel);
    }

    CoroutineScheduler* scheduler() const {
        return m_pScheduler;
    }
    CoroutineStart startInfo() const {
        return m_startInfo;
    }

    void enqueue(std::function<void()> func) {
        m_pScheduler->enqueue(std::move(func));
    }
    bool initSwitchThread() const {
        return m_startInfo == CoroutineStart::parallel || m_pScheduler->initSwitchThread();
    }
    void markRunnable(std::coroutine_handle<void> h, bool expect_end_soon) {
        m_pScheduler->markRunnable(h, expect_end_soon);
    }
    void markCompleted(const void* id) {
        m_pScheduler->markCompleted(id);
    }
    void waitFor(const void* id) {
        m_pScheduler->waitFor(id);
    }

private:
    CoroutineScheduler* m_pScheduler;
    CoroutineStart m_startInfo;
};

/** @brief An awaiter that forces the current coroutine to invoke the scheduler to probably switch the execution to some other thread.
 * */
class SwitchThreadAwaiter {
public:
    SwitchThreadAwaiter(CoroutineSchedulingInfo const& schedulingInfo)
        :m_schedulingInfo(schedulingInfo.scheduler(), schedulingInfo.startInfo())
    {
        // empty
    }
    bool await_ready() const {
        return m_schedulingInfo.startInfo() == CoroutineStart::sameThread && !m_schedulingInfo.scheduler()->initSwitchThread();
    }
    void await_suspend(std::coroutine_handle<void> thisHandler) {
        CARPAL_LOG_DEBUG("SwitchThreadAwaiter::await_suspend() making coroutine ", thisHandler.address(),
            " runnable on scheduler ", m_schedulingInfo.scheduler()->address());
        m_schedulingInfo.scheduler()->markRunnable(thisHandler, false);
        CARPAL_LOG_DEBUG("SwitchThreadAwaiter::await_suspend() after");
    }
    void await_resume() {
        CARPAL_LOG_DEBUG("SwitchThreadAwaiter::await_resume()");
        // empty
    }
private:
    CoroutineSchedulingInfo m_schedulingInfo;
};

CoroutineScheduler* defaultCoroutineScheduler();
inline
CoroutineSchedulingInfo defaultSameThreadStart() {
    return CoroutineSchedulingInfo(defaultCoroutineScheduler(), CoroutineStart::sameThread);
}
inline
CoroutineSchedulingInfo defaultParallelStart() {
    return CoroutineSchedulingInfo(defaultCoroutineScheduler(), CoroutineStart::parallel);
}

} // namespace carpal
