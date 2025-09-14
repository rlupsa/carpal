// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#pragma once

#include "carpal/config.h"
#include "carpal/Logger.h"

#ifdef ENABLE_COROUTINES
#include "carpal/CoroutineScheduler.h"
#endif
#include "carpal/PromiseFuturePair.h"
#include "carpal/FutureHelpers.h"
#include "carpal/Executor.h"

#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

namespace carpal {

template<typename T>
Future<T> exceptionFuture(std::exception_ptr ex);

/** @brief A reference exposing the consumer facing side of the promise-future pair.
 * */
template<>
class Future<void> {
public:
    using BaseType = void;
#ifdef ENABLE_COROUTINES
    class promise_type;
#endif

    explicit Future(std::nullptr_t)
        :m_pFuture(nullptr)
    {}

    /** @brief Takes over a heap allocated PromiseFuturePair.
     * */
    template<class S>
    explicit Future(IntrusiveSharedPtr<S> pf)
        :m_pFuture(pf)
    {}

    /** @brief Waits (blocking the current thread) until the underlying operation completes.*/
    void wait() const noexcept {
        return m_pFuture->wait();
    }

    /** @brief Returns true if already triggered. Does not wait.
     * @note the result can be outdated by the time the caller can use the result.*/
    bool isComplete() const noexcept {
        return m_pFuture->isComplete();
    }

    /** @brief Returns true if the future is completed normally - that is, is completed and not with an exception
     * */
    bool isCompletedNormally() const noexcept {
        return m_pFuture->isCompletedNormally();
    }

    /** @brief Returns true if the future is completed with exception
     * */
    bool isException() const noexcept {
        return m_pFuture->isException();
    }

    /** @brief Waits for the future to complete; then, if completed with exception, returns the exception, otherwise returns nullptr
     * */
    std::exception_ptr getException() const noexcept {
        return m_pFuture->getException();
    }

    /** @brief Waits (blocking the current thread) until completed, then returns or throws.*/
    void get() const {
        m_pFuture->wait();
        if(m_pFuture->isException()) {
            std::rethrow_exception(m_pFuture->getException());
        }
    }

    /** @brief Sets the given function to be executed when the future completes.
     * 
     * If the future is already completed, the callback executes immediately on the current thread;
     * otherwise, it will execute on the thread that completes the future.
     * */
    template<typename Func>
    void addSynchronousCallback(Func func) {
        m_pFuture->addSynchronousCallback(std::move(func));
    }

    IntrusiveSharedPtr<PromiseFuturePairBase> getPromiseFuturePair() const {
        return m_pFuture;
    }

    /** @brief Sets the given function to execute, on the given executor, after the current future completes.
     * @return A future that completes with the value (or exception) returned by the given function.
     * */
    template<typename Func>
    Future<typename std::invoke_result<Func>::type>
    then(Executor* pExecutor, Func func) {
        using R = typename std::invoke_result<Func>::type;
        IntrusiveSharedPtr<carpal_private::ContinuationTaskFromOneVoidFuture<R, Func> > pRet(
            new carpal_private::ContinuationTaskFromOneVoidFuture<R, Func>(
                pExecutor, std::move(func), this->getPromiseFuturePair()));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskFromOneVoidFuture<R, Func>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    /** @brief Sets the given function to execute, on the default executor, after the current future completes.
     * @return A future that completes with the value (or exception) returned by the given function.
     * */
    template<typename Func>
    Future<typename std::invoke_result<Func>::type>
    then(Func func) {
        using R = typename std::invoke_result<Func>::type;
        IntrusiveSharedPtr<carpal_private::ContinuationTaskFromOneVoidFuture<R, Func> > pRet(
            new carpal_private::ContinuationTaskFromOneVoidFuture<R, Func>(
                defaultExecutor(), std::move(func), this->getPromiseFuturePair()));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskFromOneVoidFuture<R, Func>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    /** @brief Sets the given asynchronous function to execute, on the given executor, after the current future completes.
     * @return A future that completes when the future returned by @c func completes.
     * */
    template<typename Func>
    Future<typename std::invoke_result<Func>::type::BaseType>
    thenAsync(Executor* pExecutor, Func func) {
        using R = typename std::invoke_result<Func>::type::BaseType;
        IntrusiveSharedPtr<carpal_private::ContinuationAsyncTaskFromOneVoidFuture<Func> > pRet(
            new carpal_private::ContinuationAsyncTaskFromOneVoidFuture<Func>(
                pExecutor, std::move(func), this->getPromiseFuturePair()));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationAsyncTaskFromOneVoidFuture<Func>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    /** @brief Sets the given asynchronous function to execute, on the default executor, after the current future completes.
     * @return A future that completes when the future returned by @c func completes.
     * */
    template<typename Func>
    Future<typename std::invoke_result<Func>::type::BaseType>
    thenAsync(Func func) {
        using R = typename std::invoke_result<Func>::type::BaseType;
        IntrusiveSharedPtr<carpal_private::ContinuationAsyncTaskFromOneVoidFuture<Func> > pRet(
            new carpal_private::ContinuationAsyncTaskFromOneVoidFuture<Func>(
                defaultExecutor(), std::move(func), this->getPromiseFuturePair()));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationAsyncTaskFromOneVoidFuture<Func>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    template<typename FuncCond, typename FuncBody>
    Future<void>
    thenAsyncLoop(Executor* pExecutor, FuncCond cond, FuncBody body) {
        IntrusiveSharedPtr<carpal_private::ContinuationTaskAsyncLoopVoid<void, FuncCond, FuncBody> > pRet(
            new carpal_private::ContinuationTaskAsyncLoopVoid<void, FuncCond, FuncBody>(
                pExecutor, std::move(cond), std::move(body), *this));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncLoopVoid<void, FuncCond, FuncBody>::onFutureCompleted(pRet);});
        return Future<void>(pRet);
    }

    template<typename FuncCond, typename FuncBody>
    Future<void>
    thenAsyncLoop(FuncCond cond, FuncBody body) {
        IntrusiveSharedPtr<carpal_private::ContinuationTaskAsyncLoopVoid<void, FuncCond, FuncBody> > pRet(
            new carpal_private::ContinuationTaskAsyncLoopVoid<void, FuncCond, FuncBody>(
                defaultExecutor(), std::move(cond), std::move(body), *this));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncLoopVoid<void, FuncCond, FuncBody>::onFutureCompleted(pRet);});
        return Future<void>(pRet);
    }

    template<typename Func>
    Future<void> thenCatchAll(Executor* pExecutor, Func func) {
        IntrusiveSharedPtr<carpal_private::ContinuationTaskCatchAll<void, Func> > pRet(
            new carpal_private::ContinuationTaskCatchAll<void, Func>(pExecutor, std::move(func), *this));
        this->addSynchronousCallback([pRet](){pRet->onFutureCompleted();});
        return Future<void>(pRet);
    }

    template<typename Func>
    Future<void> thenCatchAll(Func func) {
        IntrusiveSharedPtr<carpal_private::ContinuationTaskCatchAll<void, Func> > pRet(
            new carpal_private::ContinuationTaskCatchAll<void, Func> >(defaultExecutor(), std::move(func), *this));
        this->addSynchronousCallback([pRet](){pRet->onFutureCompleted();});
        return Future<void>(pRet);
    }

    template<typename Ex, typename Func>
    Future<void> thenCatch(Executor* pExecutor, Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> void {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                f(ex);
            }
        };
        IntrusiveSharedPtr<carpal_private::ContinuationTaskCatchAll<void, decltype(generalHandler)> > pRet(
            new carpal_private::ContinuationTaskCatchAll<void, decltype(generalHandler)>(
                pExecutor, std::move(generalHandler), *this));
        this->addSynchronousCallback([pRet](){pRet->onFutureCompleted();});
        return Future<void>(pRet);
    }

    template<typename Ex, typename Func>
    Future<void> thenCatch(Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> void {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                f(ex);
            }
        };
        IntrusiveSharedPtr<carpal_private::ContinuationTaskCatchAll<void, decltype(generalHandler)> > pRet(
            new carpal_private::ContinuationTaskCatchAll<void, decltype(generalHandler)>(
                defaultExecutor(), std::move(generalHandler), *this));
        this->addSynchronousCallback([pRet](){pRet->onFutureCompleted();});
        return Future<void>(pRet);
    }

    template<typename Func>
    Future<void> thenCatchAllAsync(Executor* pExecutor, Func func) {
        IntrusiveSharedPtr<carpal_private::ContinuationTaskAsyncCatchAll<void, Func> > pRet(
            new carpal_private::ContinuationTaskAsyncCatchAll<void, Func>(pExecutor, std::move(func), *this));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<void, Func>::onFutureCompleted(pRet);});
        return Future<void>(pRet);
    }

    template<typename Func>
    Future<void> thenCatchAllAsync(Func func) {
        IntrusiveSharedPtr<carpal_private::ContinuationTaskAsyncCatchAll<void, Func> > pRet(
            new carpal_private::ContinuationTaskAsyncCatchAll<void, Func> >(defaultExecutor(), std::move(func), *this));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<void, Func>::onFutureCompleted(pRet);});
        return Future<void>(pRet);
    }

    template<typename Ex, typename Func>
    Future<void> thenCatchAsync(Executor* pExecutor, Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> Future<void> {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                return f(ex);
            } catch(...) {
                return exceptionFuture<void>(std::current_exception());
            }
        };
        IntrusiveSharedPtr<carpal_private::ContinuationTaskAsyncCatchAll<void, decltype(generalHandler)> > pRet(
            new carpal_private::ContinuationTaskAsyncCatchAll<void, decltype(generalHandler)>(pExecutor, std::move(generalHandler), *this));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<void, decltype(generalHandler)>::onFutureCompleted(pRet);});
        return Future<void>(pRet);
    }

    template<typename Ex, typename Func>
    Future<void> thenCatchAsync(Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> Future<void> {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                return f(ex);
            } catch(...) {
                return exceptionFuture<void>(std::current_exception());
            }
        };
        IntrusiveSharedPtr<carpal_private::ContinuationTaskAsyncCatchAll<void, decltype(generalHandler)> > pRet(
            new carpal_private::ContinuationTaskAsyncCatchAll<void, decltype(generalHandler)>(defaultExecutor(), std::move(generalHandler), *this));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<void, decltype(generalHandler)>::onFutureCompleted(pRet);});
        return Future<void>(pRet);
    }

    /** @brief Resets the pointer to the PromiseFuturePair. Renders the Future unusable for anything but calling the destructor. */
    void reset() {
        m_pFuture.reset();
    }

private:
    IntrusiveSharedPtr<PromiseFuturePairBase> m_pFuture;
};

template<typename T>
class Future {
public:
    using BaseType = T;
#ifdef ENABLE_COROUTINES
    class promise_type;
#endif

    explicit Future(std::nullptr_t)
        :m_pFuture(nullptr)
    {}

    /** @brief Takes over a heap allocated PromiseFuturePair
     * */
    template<class S>
    explicit Future(IntrusiveSharedPtr<S> pf)
        :m_pFuture(pf)
    {}

    /** @brief Waits (blocking the current thread) until the underlying operation completes.*/
    void wait() const noexcept {
        return m_pFuture->wait();
    }

    /** @brief Returns true if already triggered. Does not wait.
     * @note the result can be outdated by the time the caller can use the result.*/
    bool isComplete() const noexcept {
        return m_pFuture->isComplete();
    }

    bool isCompletedNormally() const noexcept {
        return m_pFuture->isCompletedNormally();
    }
    
    bool isException() const noexcept {
        return m_pFuture->isException();
    }

    /** @brief Waits (blocking the current thread) until the value is available, then returns the value.*/
    T& get() const {
        return m_pFuture->get();
    }

    std::exception_ptr getException() const noexcept {
        return m_pFuture->getException();
    }

    template<typename Func>
    void addSynchronousCallback(Func func) const {
        m_pFuture->addSynchronousCallback(std::move(func));
    }

    operator Future<void>() {
        return Future<void>(m_pFuture);
    }

    IntrusiveSharedPtr<PromiseFuturePair<T> > getPromiseFuturePair() const {
        return m_pFuture;
    }

    /** @brief Resets the pointer to the PromiseFuturePair. Renders the Future unusable for anything but calling the destructor.
     * May be called after the Future is used, in order to free any associated resources.*/
    void reset() {
        m_pFuture.reset();
    }

    template<typename Func>
    Future<typename std::invoke_result<Func, T>::type>
    then(Executor* pExecutor, Func func) {
        using R = typename std::invoke_result<Func, T>::type;
        IntrusiveSharedPtr<carpal_private::ContinuationTaskFromOneFuture<R, Func, T> > pRet(new carpal_private::ContinuationTaskFromOneFuture<R, Func, T>(
            pExecutor, std::move(func), *this));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskFromOneFuture<R, Func, T>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    template<typename Func>
    Future<typename std::invoke_result<Func, T>::type>
    then(Func func) {
        using R = typename std::invoke_result<Func, T>::type;
        IntrusiveSharedPtr<carpal_private::ContinuationTaskFromOneFuture<R, Func, T> > pRet(new carpal_private::ContinuationTaskFromOneFuture<R, Func, T>(
            defaultExecutor(), std::move(func), *this));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskFromOneFuture<R, Func, T>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    template<typename Func>
    Future<typename std::invoke_result<Func, T>::type::BaseType>
    thenAsync(Executor* pExecutor, Func func) {
        using R = typename std::invoke_result<Func, T>::type::BaseType;
        IntrusiveSharedPtr<carpal_private::ContinuationAsyncTaskFromOneFuture<Func, T> > pRet(new carpal_private::ContinuationAsyncTaskFromOneFuture<Func, T>(
            pExecutor, std::move(func), this->getPromiseFuturePair()));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationAsyncTaskFromOneFuture<Func, T>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    template<typename Func>
    Future<typename std::invoke_result<Func, T>::type::BaseType>
    thenAsync(Func func) {
        using R = typename std::invoke_result<Func, T>::type::BaseType;
        IntrusiveSharedPtr<carpal_private::ContinuationAsyncTaskFromOneFuture<Func, T> > pRet(new carpal_private::ContinuationAsyncTaskFromOneFuture<Func, T>(
            defaultExecutor(), std::move(func), this->getPromiseFuturePair()));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationAsyncTaskFromOneFuture<Func, T>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    /** @brief Executes the @c body asynchronous function as long as @c cond returns true, starting on the current future.
     * @param pExecutor Pointer to the executor that will execute the loop body.
     * @param cond The looping condition. Must be defined as @c bool @c cond(T)
     * @param body The loop body. Defined as @c Future<T> @c body(T)
     * @return A future that completes when the @c cond function returns false
     * 
     * When the current future completes, the @c cond function (which must take a @c T and return a @c bool) is executed. If it returns
     * false, the returned future completes with this future value. If @c cond returns true, @c body is executed on the value of the current
     * future. Then, when it completes, the condition is evaluated again on its value and, if true, the body is executed again.
     * 
     * If the current future completes with an exception, or if the body throws an exception or the future it returns completes with an
     * exception, the loop ends and the returned future completes with that exception.
     * */
    template<typename FuncCond, typename FuncBody>
    Future<T>
    thenAsyncLoop(Executor* pExecutor, FuncCond cond, FuncBody body) {
        IntrusiveSharedPtr<carpal_private::ContinuationTaskAsyncLoop<T, FuncCond, FuncBody> > pRet(new carpal_private::ContinuationTaskAsyncLoop<T, FuncCond, FuncBody>(
            pExecutor, std::move(cond), std::move(body), *this));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncLoop<T, FuncCond, FuncBody>::onFutureCompleted(pRet);});
        return Future<T>(pRet);
    }

    template<typename FuncCond, typename FuncBody>
    Future<T>
    thenAsyncLoop(FuncCond cond, FuncBody body) {
        IntrusiveSharedPtr<carpal_private::ContinuationTaskAsyncLoop<T, FuncCond, FuncBody> > pRet(new carpal_private::ContinuationTaskAsyncLoop<T, FuncCond, FuncBody>(
            defaultExecutor(), std::move(cond), std::move(body), *this));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncLoop<T, FuncCond, FuncBody>::onFutureCompleted(pRet);});
        return Future<T>(pRet);
    }

    template<typename Func>
    Future<T> thenCatchAll(Executor* pExecutor, Func func) {
        IntrusiveSharedPtr<carpal_private::ContinuationTaskCatchAll<T, Func> > pRet(new carpal_private::ContinuationTaskCatchAll<T, Func>(pExecutor, std::move(func), *this));
        this->addSynchronousCallback([pRet](){pRet->onFutureCompleted();});
        return Future<T>(pRet);
    }

    template<typename Func>
    Future<T> thenCatchAll(Func func) {
        IntrusiveSharedPtr<carpal_private::ContinuationTaskCatchAll<T, Func> > pRet(new carpal_private::ContinuationTaskCatchAll<T, Func>(defaultExecutor(), std::move(func), *this));
        this->addSynchronousCallback([pRet](){pRet->onFutureCompleted();});
        return Future<T>(pRet);
    }

    template<typename Ex, typename Func>
    Future<T> thenCatch(Executor* pExecutor, Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> T {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                return f(ex);
            }
        };
        IntrusiveSharedPtr<carpal_private::ContinuationTaskCatchAll<T, decltype(generalHandler)> > pRet(new carpal_private::ContinuationTaskCatchAll<T, decltype(generalHandler)>(
            pExecutor, std::move(generalHandler), *this));
        this->addSynchronousCallback([pRet](){pRet->onFutureCompleted();});
        return Future<T>(pRet);
    }

    template<typename Ex, typename Func>
    Future<T> thenCatch(Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> T {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                return f(ex);
            }
        };
        IntrusiveSharedPtr<carpal_private::ContinuationTaskCatchAll<T, decltype(generalHandler)> > pRet(new carpal_private::ContinuationTaskCatchAll<T, decltype(generalHandler)>(
            defaultExecutor(), std::move(generalHandler), *this));
        this->addSynchronousCallback([pRet](){pRet->onFutureCompleted();});
        return Future<T>(pRet);
    }

    template<typename Func>
    Future<T> thenCatchAllAsync(Executor* pExecutor, Func func) {
        IntrusiveSharedPtr<carpal_private::ContinuationTaskAsyncCatchAll<T, Func> > pRet(new carpal_private::ContinuationTaskAsyncCatchAll<T, Func>(pExecutor, std::move(func), *this));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<T, Func>::onFutureCompleted(pRet);});
        return Future<T>(pRet);
    }

    template<typename Func>
    Future<T> thenCatchAllAsync(Func func) {
        IntrusiveSharedPtr<carpal_private::ContinuationTaskAsyncCatchAll<T, Func> > pRet(new carpal_private::ContinuationTaskAsyncCatchAll<T, Func>(defaultExecutor(), std::move(func), *this));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<T, Func>::onFutureCompleted(pRet);});
        return Future<T>(pRet);
    }

    template<typename Ex, typename Func>
    Future<T> thenCatchAsync(Executor* pExecutor, Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> Future<T> {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                return f(ex);
            } catch(...) {
                return exceptionFuture<T>(std::current_exception());
            }
        };
        IntrusiveSharedPtr<carpal_private::ContinuationTaskAsyncCatchAll<T, decltype(generalHandler)> > pRet(new carpal_private::ContinuationTaskAsyncCatchAll<T, decltype(generalHandler)>(pExecutor,
            std::move(generalHandler), *this));
        this->addSynchronousCallback([pRet](){
            carpal_private::ContinuationTaskAsyncCatchAll<T, decltype(generalHandler)>::onFutureCompleted(pRet);
        });
        return Future<T>(pRet);
    }

    template<typename Ex, typename Func>
    Future<T> thenCatchAsync(Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> Future<T> {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                return f(ex);
            } catch(...) {
                return exceptionFuture<T>(std::current_exception());
            }
        };
        IntrusiveSharedPtr<carpal_private::ContinuationTaskAsyncCatchAll<T, decltype(generalHandler)> > pRet(new carpal_private::ContinuationTaskAsyncCatchAll<T, decltype(generalHandler)>(defaultExecutor(),
            std::move(generalHandler), *this));
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<T, decltype(generalHandler)>::onFutureCompleted(pRet);});
        return Future<T>(pRet);
    }

private:
    IntrusiveSharedPtr<PromiseFuturePair<T> > m_pFuture;
};

/** @brief A reference to the promise side of a promise-future pair */
template<typename T>
class Promise {
public:
    /** @brief Creates the promise-future pair */
    Promise()
        :m_pf(new PromiseFuturePair<T>)
    {}

    /** @brief Sets the value into the promise-future pair. Makes the Future side complete.
    *    This function must be called exactly once in the lifetime of the Promise!*/
    void set(T val) const {
        m_pf->set(std::move(val));
    }

    void setException(std::exception_ptr exception) {
        m_pf->setException(exception);
    }

    /** @brief Returns the Future side of the promise-future pair
     */
    Future<T> future() const {
        return Future<T>(m_pf);
    }
private:
    IntrusiveSharedPtr<PromiseFuturePair<T> > m_pf;
};

template<>
class Promise<void> {
public:
    /** @brief Creates the promise-future pair */
    Promise() :m_pf(new PromiseFuturePair<void>)
    {}

    /** @brief Makes the Future side complete.
    *    This function must be called exactly once in the lifetime of the Promise!*/
    void set() const {
        m_pf->set();
    }

    void setException(std::exception_ptr exception) {
        m_pf->setException(exception);
    }

    /** @brief Returns the Future side of the promise-future pair
     */
    Future<void> future() const {
        return Future<void>(m_pf);
    }
private:
    IntrusiveSharedPtr<PromiseFuturePair<void> > m_pf;
};

/**
 * @brief Starts an asynchrounous computation
 * @warning Someone must keep the returned future and wait on it to complete! Destroying the returned future without waiting on it will lead to undefined behavior!
 * @param tp The thread pool to use for executing the computation
 * @param func The computation to be executed. Must be a function taking no arguments and returning a value of type R
 * @return A future that completes when the function finishes execution, and can be used to obtain the returned value.
 */
template<typename Func>
Future<typename std::invoke_result<Func>::type>
runAsync(Executor* tp, Func func) {
    using R = typename std::invoke_result<Func>::type;
    IntrusiveSharedPtr<carpal_private::ReadyTask<R, Func> > pf(new carpal_private::ReadyTask<R, Func>(std::move(func)));
    tp->enqueue([pf](){pf->execute();});
    return Future<R>(pf);
}

/**
 * @brief Starts an asynchrounous computation on the default thread pool
 * @warning Someone must keep the returned future and wait on it to complete! Destroying the returned future without waiting on it will lead to undefined behavior!
 * @param func The computation to be executed. Must be a function taking no arguments and returning a value of type R
 * @return A future that completes when the function finishes execution, and can be used to obtain the returned value.
 */
template<typename Func>
Future<typename std::invoke_result<Func>::type>
runAsync(Func func) {
    return runAsync(defaultExecutor(), std::move(func));
}

namespace carpal_private {

/**
 * @brief Given a completed future start, it executes loopingPredicate on it and, as long as it returns true, it enqueues 
 * @param executor
 * @param loopingPredicate
 * @param loopFunc
 * @param start
 * @param ret
 */
template<typename R, typename LoopFunc, typename PredicateFunc>
void auxLoop(Executor* pExecutor, PredicateFunc loopingPredicate, LoopFunc loopFunc, R const& start, IntrusiveSharedPtr<PromiseFuturePair<R> > ret)
{
    if(!loopingPredicate(start)) {
        ret->set(start);
        return;
    }

    try {
        Future<R> tmpResFuture = loopFunc(start);
        tmpResFuture.addSynchronousCallback([pExecutor,loopingPredicate,loopFunc,tmpResFuture,ret](){
            pExecutor->enqueue([pExecutor,loopingPredicate,loopFunc,tmpResFuture,ret](){
                if(tmpResFuture.isException()) {
                    ret->setException(tmpResFuture.getException());
                } else {
                    auxLoop(pExecutor, loopingPredicate, loopFunc, tmpResFuture.get(), ret);
                }
            });
        });
    } catch(...) {
        ret->setException(std::current_exception());
    }
}
} // namespace carpal_private

/**
 * @brief Arranges that the given function executes when all pre-requisites are available. This version takes a function that takes the actual values as arguments.
 * @warning Someone must keep the returned future and wait on it to complete! Destroying the returned future without waiting on it will lead to undefined behavior!
 * @param pTp The thread pool to use for executing the computation
 * @param func The computation to be executed. Must be a function taking arguments corresponding to the pre-requisites (the values returned by the futures) and returning a value of type R
 * @param futures The futures that represent the pre-requisites of the computation. The computation will not start before they are all complete.
 * @return A future that completes when the function finishes execution, and can be used to obtain the returned value.
 * 
 * @note This version has the function representing the computation take the actual values. This means that this cannot be used if some of the futures are Future<void>
 * @see whenAllFromFutures()
 */
template<typename Func, typename... T>
Future<typename std::invoke_result<Func, T&...>::type>
whenAll(Executor* pTp, Func func, Future<T>... futures) {
    using R = typename std::invoke_result<Func, T&...>::type;
    auto fwdFunc = [func](Future<T>... ff) -> R {return func(ff.get()...);};
    IntrusiveSharedPtr<carpal_private::ContinuationTask<R, decltype(fwdFunc), Future<T>...> > pRet(
        new carpal_private::ContinuationTask<R, decltype(fwdFunc), Future<T>...>(
        pTp, fwdFunc, futures...));
    carpal_private::attachContinuations<sizeof...(T)>(pRet);
    return Future<R>(pRet);
}

/**
 * @brief Arranges that the given function executes when all pre-requisites are available. This version takes a function that takes the actual values as arguments.
 * @warning Someone must keep the returned future and wait on it to complete! Destroying the returned future without waiting on it will lead to undefined behavior!
 * @param func The computation to be executed. Must be a function taking arguments corresponding to the pre-requisites (the values returned by the futures) and returning a value of type R
 * @param futures The futures that represent the pre-requisites of the computation. The computation will not start before they are all complete.
 * @return A future that completes when the function finishes execution, and can be used to obtain the returned value.
 * 
 * @note This version has the function representing the computation take the actual values. This means that this cannot be used if some of the futures are Future<void>
 * @see whenAllFromFutures()
 */
template<typename Func, typename... T>
Future<typename std::invoke_result<Func, T&...>::type>
whenAll(Func func, Future<T>... futures) {
    using R = typename std::invoke_result<Func, T&...>::type;
    auto fwdFunc = [func](Future<T>... ff) -> R {return func(ff.get()...);};
    IntrusiveSharedPtr<carpal_private::ContinuationTask<R, decltype(fwdFunc), Future<T>...> > pRet(
        new carpal_private::ContinuationTask<R, decltype(fwdFunc), Future<T>...>(
        defaultExecutor(), fwdFunc, futures...));
    carpal_private::attachContinuations<sizeof...(T)>(pRet);
    return Future<R>(pRet);
}

/**
 * @brief Arranges that the given function executes when all pre-requisites are available. This version takes a function that takes futures as arguments.
 * @warning Someone must keep the returned future and wait on it to complete! Destroying the returned future without waiting on it will lead to undefined behavior!
 * @param pTp The thread pool to use for executing the computation
 * @param func The computation to be executed. Must be a function taking arguments of types Future<...> corresponding to the pre-requisites and returning a value of type R
 * @param futures The futures that represent the pre-requisites of the computation. The computation will not start before they are all complete.
 * @return A future that completes when the function finishes execution, and can be used to obtain the returned value.
 * 
 * @see addContinuation()
 */
template<typename Func, typename... T>
Future<typename std::invoke_result<Func, Future<T>...>::type>
whenAllFromFutures(Executor* pTp, Func func, Future<T>... futures) {
    using R = typename std::invoke_result<Func, Future<T>...>::type;
    IntrusiveSharedPtr<carpal_private::ContinuationTask<R, Func, Future<T>...> > pRet(
        new carpal_private::ContinuationTask<R, Func, Future<T>...>(pTp, func, futures...));
    carpal_private::attachContinuations<sizeof...(T)>(pRet);
    return Future<R>(pRet);
}

/**
 * @brief Arranges that the given function executes when all pre-requisites are available. This version takes a function that takes futures as arguments.
 * @warning Someone must keep the returned future and wait on it to complete! Destroying the returned future without waiting on it will lead to undefined behavior!
 * @param pTp The thread pool to use for executing the computation
 * @param func The computation to be executed. Must be a function taking arguments of types Future<...> corresponding to the pre-requisites and returning a value of type R
 * @param futures The futures that represent the pre-requisites of the computation. The computation will not start before they are all complete.
 * @return A future that completes when the function finishes execution, and can be used to obtain the returned value.
 * 
 * @see addContinuation()
 */
template<typename Func, typename... T>
Future<typename std::invoke_result<Func, Future<T>...>::type>
whenAllFromFutures(Func func, Future<T>... futures) {
    using R = typename std::invoke_result<Func, Future<T>...>::type;
    IntrusiveSharedPtr<carpal_private::ContinuationTask<R, Func, Future<T>...> > pRet(
        new carpal_private::ContinuationTask<R, Func, Future<T>...>(defaultExecutor(), func, futures...));
    carpal_private::attachContinuations<sizeof...(T)>(pRet);
    return Future<R>(pRet);
}

template<typename Func, typename T>
Future<typename std::invoke_result<Func, std::vector<Future<T> > >::type>
whenAllFromArrayOfFutures(Executor* pTp, Func func, std::vector<Future<T> > futures) {
    using R = typename std::invoke_result<Func, std::vector<Future<T> > >::type;
    IntrusiveSharedPtr<carpal_private::ContinuationTaskArray<R, Func, T> > pRet(
        new carpal_private::ContinuationTaskArray<R, Func, T>(pTp, std::move(func), std::move(futures)));
    carpal_private::ContinuationTaskArray<R, Func, T>::attachContinuations(pRet);
    return Future<R>(pRet);
}

template<typename Func, typename T>
Future<typename std::invoke_result<Func, std::vector<Future<T> > >::type>
whenAllFromArrayOfFutures(Func func, std::vector<Future<T> > futures) {
    using R = typename std::invoke_result<Func, std::vector<Future<T> > >::type;
    IntrusiveSharedPtr<carpal_private::ContinuationTaskArray<R, Func, T> > pRet(
        new carpal_private::ContinuationTaskArray<R, Func, T>(defaultExecutor(), std::move(func), std::move(futures)));
    carpal_private::ContinuationTaskArray<R, Func, T>::attachContinuations(pRet);
    return Future<R>(pRet);
}

/**
 * @brief A function that returns an already completed future, with the provided value.
 */
template<typename T>
Future<T> completedFuture(T val) {
    Promise<T> p;
    p.set(std::move(val));
    return p.future();
}

/**
 * @brief A function that returns an already completed future of type void.
 */
inline
Future<void> completedFuture() {
    Promise<void> p;
    p.set();
    return p.future();
}

/**
 * @brief A function that returns a future completed with exception
 */
template<typename T>
inline
Future<T> exceptionFuture(std::exception_ptr ex) {
    Promise<T> p;
    p.setException(ex);
    return p.future();
}

/**
 * @brief Adds a loop of an asynchronous function as a continuation to a future
 * @param executor An executor that will execute the continuations
 * @param loopingPredicate A function taking a value of type R and returns true if the loop should execute (again) or false if the value is to be returned
 * @param loopFunc A function taking an R and returning a Future<R> that is to be used as the loop body
 * @param start 
 * @return 
 * 
 * When start completes, the loopingPredicate(start) is invoked. If this returns false, the value in start is set in
 * the retuned future. Otherwise, loopFunc(start) is invoked and its result is treated as if it were start
 */
template<typename R, typename LoopFunc, typename PredicateFunc>
Future<R> executeAsyncLoop(Executor* pExecutor, PredicateFunc loopingPredicate, LoopFunc loopFunc, R const& start)
{
    IntrusiveSharedPtr<PromiseFuturePair<R> > ret(new PromiseFuturePair<R>());
    carpal_private::auxLoop(pExecutor, loopingPredicate, loopFunc, start, ret);
    return Future<R>(ret);
}

class FutureWaiter {
public:
    FutureWaiter();
    ~FutureWaiter();
    void add(Future<void> future);
    void waitAll();

private:
    void onFutureComplete(std::list<Future<void> >::iterator );

    std::mutex m_mtx;
    std::condition_variable m_cond;
    std::list<Future<void> > m_futures;
};

#ifdef ENABLE_COROUTINES

template<typename T>
class FutureAwaiter {
public:
    FutureAwaiter(CoroutineScheduler* pScheduler, Future<T> future)
        :m_pScheduler(pScheduler),
        m_pFuture(std::move(future))
    {
        // nothing else
    }

    bool await_ready() const {
        return m_pFuture.isComplete();
    }
    void await_suspend(std::coroutine_handle<void> thisHandler) {
        CoroutineScheduler* pScheduler = m_pScheduler;
        m_pFuture.addSynchronousCallback([pScheduler, thisHandler]() {
            pScheduler->markRunnable(thisHandler);
        });
    }
    T await_resume() {
        return m_pFuture.get();
    }
private:
    CoroutineScheduler* m_pScheduler;
    Future<T> m_pFuture;
};

template<typename T>
FutureAwaiter<T> create_awaiter(CoroutineScheduler* pScheduler, Future<T> future) {
    return FutureAwaiter<T>(pScheduler, std::move(future));
}

class PromiseFuturePairBaseFinalAwaiter {
public:
    explicit PromiseFuturePairBaseFinalAwaiter(PromiseFuturePairBase* ptr) noexcept
        :m_ptr(ptr){}
    bool await_ready() const noexcept {
        return false;
    }
    void await_suspend(std::coroutine_handle<void> thisHandler) noexcept {
        m_ptr->removeRef();
    }
    void await_resume() const noexcept {
        assert(false);
    }
private:
    PromiseFuturePairBase* m_ptr;
};

template<typename T>
class Future<T>::promise_type : public PromiseFuturePair<T> {
public:
    promise_type()
        :m_pScheduler(defaultCoroutineScheduler())
    {
        CARPAL_LOG_DEBUG("Asynchronous coroutine promise object created @", static_cast<void const*>(this),
            " on default scheduler @", static_cast<void const*>(m_pScheduler));
        this->addRef(); // this reference will be removed in final_suspend()
    }

    ~promise_type() {
        CARPAL_LOG_DEBUG("Asynchronous coroutine promise object @", static_cast<void const*>(this), " destroyed");
    }

    std::suspend_never initial_suspend() {
        return std::suspend_never();
    }

    PromiseFuturePairBaseFinalAwaiter final_suspend() noexcept {
        return PromiseFuturePairBaseFinalAwaiter(this);
    }

    void unhandled_exception() {
        this->setException(std::current_exception());
    }

    Future<T> get_return_object() {
        this->m_handle = std::coroutine_handle<Future<T>::promise_type>::from_promise(*this);
        CARPAL_LOG_DEBUG("Future coroutine handle=", m_handle.address(), " created; promise object @", static_cast<void const*>(this));
        return Future<T>(IntrusiveSharedPtr<promise_type>(this));
    }

    void return_value(T val) {
        this->set(std::move(val));
    }

    SwitchThreadAwaiter await_transform(CoroutineSchedulingInfo const& newSchedulingInfo) {
        CARPAL_LOG_DEBUG("Switching coroutine @", static_cast<void const*>(this), " to scheduler @", static_cast<void const*>(newSchedulingInfo.scheduler()));
        m_pScheduler = newSchedulingInfo.scheduler();
        return SwitchThreadAwaiter(newSchedulingInfo);
    }
    SwitchThreadAwaiter await_transform(CoroutineSchedulingInfo&& newSchedulingInfo) {
        CARPAL_LOG_DEBUG("Switching coroutine @", static_cast<void const*>(this), " to scheduler @", static_cast<void const*>(newSchedulingInfo.scheduler()));
        m_pScheduler = newSchedulingInfo.scheduler();
        return SwitchThreadAwaiter(newSchedulingInfo);
    }
    SwitchThreadAwaiter await_transform(CoroutineSchedulingInfo& newSchedulingInfo) {
        CARPAL_LOG_DEBUG("Switching coroutine @", static_cast<void const*>(this), " to scheduler @", static_cast<void const*>(newSchedulingInfo.scheduler()));
        m_pScheduler = newSchedulingInfo.scheduler();
        return SwitchThreadAwaiter(newSchedulingInfo);
    }

    template<typename What>
    auto await_transform(What&& what) {
        return create_awaiter(m_pScheduler, std::forward<What>(what));
    }

    void dispose() override {
        m_handle.destroy();
    }

private:
    friend class Future<T>;

    CoroutineScheduler* m_pScheduler;
    std::coroutine_handle<Future<T>::promise_type> m_handle;
};

//template<>
class Future<void>::promise_type : public PromiseFuturePair<void> {
public:
    promise_type()
        :m_pScheduler(defaultCoroutineScheduler())
    {
        CARPAL_LOG_DEBUG("Asynchronous coroutine promise object created @", static_cast<void const*>(this),
            " on default scheduler @", static_cast<void const*>(m_pScheduler));
        this->addRef(); // this reference will be removed in final_suspend()
    }

    ~promise_type() {
        CARPAL_LOG_DEBUG("Asynchronous coroutine promise object @", static_cast<void const*>(this), " destroyed");
    }

    std::suspend_never initial_suspend() {
        return std::suspend_never();
    }

    PromiseFuturePairBaseFinalAwaiter final_suspend() noexcept {
        return PromiseFuturePairBaseFinalAwaiter(this);
    }

    void unhandled_exception() {
        this->setException(std::current_exception());
    }

    Future<void> get_return_object() {
        this->m_handle = std::coroutine_handle<Future<void>::promise_type>::from_promise(*this);
        CARPAL_LOG_DEBUG("Future coroutine handle=", m_handle.address(), " created; promise object @", static_cast<void const*>(this));
        return Future<void>(IntrusiveSharedPtr<promise_type>(this));
    }

    void return_void() {
        this->set();
    }

    SwitchThreadAwaiter await_transform(CoroutineSchedulingInfo const& newSchedulingInfo) {
        CARPAL_LOG_DEBUG("Switching coroutine @", static_cast<void const*>(this), " to scheduler @", static_cast<void const*>(newSchedulingInfo.scheduler()));
        m_pScheduler = newSchedulingInfo.scheduler();
        return SwitchThreadAwaiter(newSchedulingInfo);
    }
    SwitchThreadAwaiter await_transform(CoroutineSchedulingInfo&& newSchedulingInfo) {
        CARPAL_LOG_DEBUG("Switching coroutine @", static_cast<void const*>(this), " to scheduler @", static_cast<void const*>(newSchedulingInfo.scheduler()));
        m_pScheduler = newSchedulingInfo.scheduler();
        return SwitchThreadAwaiter(newSchedulingInfo);
    }
    SwitchThreadAwaiter await_transform(CoroutineSchedulingInfo& newSchedulingInfo) {
        CARPAL_LOG_DEBUG("Switching coroutine @", static_cast<void const*>(this), " to scheduler @", static_cast<void const*>(newSchedulingInfo.scheduler()));
        m_pScheduler = newSchedulingInfo.scheduler();
        return SwitchThreadAwaiter(newSchedulingInfo);
    }

    template<typename What>
    auto await_transform(What&& what) {
        return create_awaiter(m_pScheduler, std::forward<What>(what));
    }

    void dispose() override {
        m_handle.destroy();
    }

private:
    friend class Future<void>;

    CoroutineScheduler* m_pScheduler;
    std::coroutine_handle<Future<void>::promise_type> m_handle;
};

#endif

} // namespace carpal
