// Copyright Radu Lupsa 2023-2024
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

/**
 * This file contains the basics...
 * */

#pragma once

#include "carpal/IntrusiveSharedPtr.h"

#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include <assert.h>

namespace carpal {

/** @brief Base class for @see PromiseFuturePair, that signals the completion of an asynchronous process.
*/
class PromiseFuturePairBase {
public:
    using CallbackType = std::function<void()>;

    /** @brief The possible current state of an asynchronous computation
     * */
    enum class State : uint8_t {
        not_completed, ///< @brief Not completed (yet)
        completed_normally, ///< @brief Completed normally (no exception)
        exception ///< @brief Completed with an exception
    };

    PromiseFuturePairBase() = default;
    PromiseFuturePairBase(PromiseFuturePairBase const&) = delete;
    PromiseFuturePairBase(PromiseFuturePairBase&&) = delete;
    PromiseFuturePairBase& operator=(PromiseFuturePairBase const&) = delete;
    PromiseFuturePairBase& operator=(PromiseFuturePairBase&&) = delete;

    virtual ~PromiseFuturePairBase();

    void addRef() {
        m_refcount.fetch_add(1);
    }

    void removeRef() {
        auto const old = m_refcount.fetch_sub(1);
        assert(old >= 1);
        if(old == 1) {
            dispose();
        }
    }

    /** @brief Waits (blocking the current thread) until the asynchronous computation completes.*/
    void wait() const noexcept {
        if(m_state != State::not_completed) return;
        std::unique_lock<std::mutex> lck(m_mtx);
        while(m_state == State::not_completed) m_cv.wait(lck);
    }

    /** @brief Returns true if already completed. Does not wait.
     * @note a false result can be outdated by the time the caller can use the result.*/
    bool isComplete() const noexcept {
        return m_state != State::not_completed;
    }

    /** @brief Returns true if the asynchronous computation completed normally (without throwing an exception). Does not wait.
     * @note a false result can be outdated by the time the caller can use the result.*/
    bool isCompletedNormally() const noexcept {
        return m_state == State::completed_normally;
    }

    /** @brief Returns true if the asynchronous computation completed by throwing an exception. Does not wait.
     * @note a false result can be outdated by the time the caller can use the result.*/
    bool isException() const noexcept {
        return m_state == State::exception;
    }

    /** @brief Waits until the asynchronous computation completes (if not completed yet), then returns the exception (if
     * the asynchronous computation throws) or nullptr (if it completes normally). */
    std::exception_ptr getException() const noexcept {
        wait();
        return m_exception;
    }

    /** @brief Adds a callback that will get called when the event is triggered.
    *
    * If the event is already triggered, the callback will be called immediately on the current thread, before this
    * function returns. If the event is not triggered yet, then the callback will be called on the thread
    * calling @see notify().
    *
    * @note The caller must make sure that the function remains valid (no dangling pointers) until it gets
    * executed.
    */
    void addSynchronousCallback(CallbackType callback);

protected:
    /** @brief Marks the computation complete. Must be called exactly once.*/
    void notify(State state);

    virtual void dispose();

protected:
    std::atomic<int32_t> m_refcount {0};
    mutable std::mutex m_mtx;
    mutable std::condition_variable m_cv;
    std::atomic<State> m_state{State::not_completed};
    std::exception_ptr m_exception = nullptr;
    CallbackType m_continuations = nullptr;
};

/** @brief A channel by which a consumer can get a value that will be produced by a producer at some
* future time.
*
* @note There is a specialization for @c void because one cannot handle a @c void value. Producers of @c void will have pointers to
* @c PromiseFuturePair<void>, while consumers will have pointers to @c PromiseFuturePairBase because consumers of @c void can use values of
* any type. However, general conversion are not allowed - it is not possible for a consumer of @c long, to use a @c PromiseFuturePair<int>.
*/
template<typename T>
class PromiseFuturePair : public PromiseFuturePairBase {
public:
    using BaseType = T;
    using ConsumerFacingType = PromiseFuturePair<T>;

    /** @brief Waits (blocking the current thread) until the value is available, then returns the value.
     * @note The value is returned by non-const reference, allowing the consumer to move it away. It is user's responsibility
     * to make sure this is not used with multiple consumers. */
    T& get() {
        this->wait();
        if(m_state == State::completed_normally) {
            return m_val.value();
        } else {
            std::rethrow_exception(m_exception);
        }
    }

    /** @brief Sets the value into the channel. Must be called exactly once.*/
    void set(T val) {
        m_val = std::optional<T>(std::move(val));
        this->notify(State::completed_normally);
    }

    /** @brief Executes the given function and sets the result as the value of the future. If the function ends in exception,
     * it sets the exception into the future.
     * @param func The function to be executed. Must return a type convertible to @c T
     * @param args The arguments to be passed (forwarded) to the function
     *
     * @note This function is provided to simplify generic code that also works with @c PromiseFuturePair<void>
     */
    template<typename Func, typename... Args>
    void computeAndSet(Func&& func, Args&&... args) noexcept {
        try {
            this->set(std::forward<Func>(func)(std::forward<Args>(args)...));
        } catch(...) {
            this->setException(std::current_exception());
        }
    }

    /** @brief Sets the value by moving it from the specified future.
     * 
     * @note This function is provided to simplify generic code that also works with @c PromiseFuturePair<void>
     * */
    void setFromOtherFutureMove(IntrusiveSharedPtr<PromiseFuturePair<T> > pF) {
        if(pF->isCompletedNormally()) {
            this->set(std::move(pF->get()));
        } else {
            this->setException(pF->getException());
        }
    }

    /** @brief Executes the function and sets the result as the value of the future.
     * @param func The function to be executed. Must return a type convertible to @c T
     * @param args A tuple whose components shall be passed (forwarded) to the function as independent arguments
     *
     * @note This function is provided to simplify generic code that also works with @c PromiseFuturePair<void>
     */
    template<typename Func, typename... Args>
    void computeAndSetWithTuple(Func func, std::tuple<Args...> args) noexcept {
        try {
            this->set(std::apply(std::move(func), std::move(args)));
        } catch(...) {
            this->setException(std::current_exception());
        }
    }

    /** @brief Completes the future with the specified exception.
     * */
    void setException(std::exception_ptr exception) {
        m_exception = exception;
        notify(State::exception);
    }

private:
    std::optional<T> m_val;
};

/** @brief @c PromiseFuturePair specialization for void
 * */
template<>
class PromiseFuturePair<void> : public PromiseFuturePairBase {
public:
    using BaseType = void;
    using ConsumerFacingType = PromiseFuturePairBase;

    /** @brief Waits (blocking the current thread) until the future completes.*/
    void get() {
        this->wait();
        if(m_state == State::exception) {
            std::rethrow_exception(m_exception);
        }
    }

    /** @brief Marks the asynchronous operation as completed normally.*/
    void set() {
        this->notify(State::completed_normally);
    }

    /** Executes the specified function with the specified arguments (forwarded), then sets the future as being completed
     * (normally, if the function completes normally, or with the exception thrown by the function).
     * 
     * @note This function is provided to simplify writing generic code.
     * */
    template<typename Func, typename... Args>
    void computeAndSet(Func&& func, Args&&... args) noexcept {
        try {
            std::forward<Func>(func)(std::forward<Args>(args)...);
            this->notify(State::completed_normally);
        } catch(...) {
            this->setException(std::current_exception());
        }
    }

    /** @brief If the given future is completed normally, sets the this future as completed normally; otherwise, sets the this future
     * as completed with the same exception as the one given as an argument.
     * 
     * @note This function is provided to simplify writing generic code.
     * */
    void setFromOtherFutureMove(IntrusiveSharedPtr<PromiseFuturePairBase> pF) {
        if(pF->isCompletedNormally()) {
            this->notify(State::completed_normally);
        } else {
            this->setException(pF->getException());
        }
    }

    /** @brief Executes the function and sets the result as the value of the future.
     * @param func The function to be executed. Must return a type convertible to @c T
     * @param args A tuple whose components shall be passed (forwarded) to the function as independent arguments
     *
     * @note This function is provided to simplify writing generic code.
     */
    template<typename Func, typename... Args>
    void computeAndSetWithTuple(Func func, std::tuple<Args...> args) noexcept {
        std::apply(std::move(func), std::move(args));
        this->notify(State::completed_normally);
    }

    /** @brief Completes the future with the specified exception.
     * */
    void setException(std::exception_ptr exception) {
        m_exception = exception;
        notify(State::exception);
    }

};

}
