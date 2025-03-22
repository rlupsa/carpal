// Copyright Radu Lupsa 2023-2024
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

/**
 * This file contains the basics...
 * */

#pragma once

#include "carpal/Executor.h"
#include "carpal/PromiseFuturePair.h"

#include <functional>
#include <memory>
#include <optional>

namespace carpal {

template<typename T>
class Future;


namespace carpal_private {

/** @brief [Internal use] A task that is ready to start executing (does not depend on other futures) */
template<typename R, typename Func>
class ReadyTask : public PromiseFuturePair<R> {
public:
    explicit ReadyTask(Func func)
        : m_func(std::move(func))
    {}
    void execute() noexcept {
        this->computeAndSet(std::move(m_func));
    }
private:
    Func m_func;
};

/** @brief [Internal use] A task that depends on a single future to start executing (can start as soon as that future completes) */
template<typename R, typename Func, typename T>
class ContinuationTaskFromOneFuture : public PromiseFuturePair<R> {
public:
    ContinuationTaskFromOneFuture(Executor* pExecutor, Func func, Future<T> future)
        :m_pExecutor(pExecutor),
        m_func(std::move(func)),
        m_future(future)
    {
    }

    static void onFutureCompleted(IntrusiveSharedPtr<ContinuationTaskFromOneFuture> pThis) {
        if(pThis->m_future.isCompletedNormally()) {
            pThis->m_pExecutor->enqueue([pThis]() noexcept {
                pThis->computeAndSet(std::move(pThis->m_func), pThis->m_future.get());
                pThis->m_future.reset();
            });
        } else {
            pThis->setException(pThis->m_future.getException());
            pThis->m_future.reset();
        }
    }

private:
    Executor* m_pExecutor;
    Func m_func;
    Future<T> m_future;
};

/** @brief [Internal use] A task that depends on a single void future to start executing (can start as soon as that future completes) */
template<typename R, typename Func>
class ContinuationTaskFromOneVoidFuture : public PromiseFuturePair<R> {
public:
    ContinuationTaskFromOneVoidFuture(Executor* pExecutor, Func func, IntrusiveSharedPtr<PromiseFuturePairBase > pFuture)
        :m_pExecutor(pExecutor),
        m_func(std::move(func)),
        m_pFuture(pFuture)
    {
    }

    static void onFutureCompleted(IntrusiveSharedPtr<ContinuationTaskFromOneVoidFuture> pThis) {
        if(pThis->m_pFuture->isCompletedNormally()) {
            pThis->m_pExecutor->enqueue([pThis]() noexcept {
                pThis->computeAndSet(std::move(pThis->m_func));
                pThis->m_pFuture.reset();
            });
        } else {
            pThis->setException(pThis->m_pFuture->getException());
            pThis->m_pFuture.reset();
        }
    }

private:
    Executor* m_pExecutor;
    Func m_func;
    IntrusiveSharedPtr<PromiseFuturePairBase> m_pFuture;
};

/** @brief [Internal use] A task that depends on a single future to start executing (can start as soon as that future completes),
will take the value via const reference, and executes an asynchronous operation returning a future. */
template<typename Func, typename T>
class ContinuationAsyncTaskFromOneFuture : public PromiseFuturePair<typename std::invoke_result<Func,T>::type::BaseType> {
public:
    using ReturnFuture = typename std::invoke_result<Func,T>::type;
    using ReturnType = typename ReturnFuture::BaseType;
    ContinuationAsyncTaskFromOneFuture(Executor* pExecutor, Func func, IntrusiveSharedPtr<PromiseFuturePair<T> > pFuture)
        :m_pExecutor(pExecutor),
        m_func(std::move(func)),
        m_pAntecessorFuture(pFuture)
    {
    }

    static void onFutureCompleted(IntrusiveSharedPtr<ContinuationAsyncTaskFromOneFuture<Func, T> > pThis) {
        if(pThis->m_pAntecessorFuture->isCompletedNormally()) {
            pThis->m_pExecutor->enqueue([pThis]() noexcept {
                pThis->m_pAsyncOpFuture = pThis->m_func(pThis->m_pAntecessorFuture->get()).getPromiseFuturePair();
                pThis->m_pAsyncOpFuture->addSynchronousCallback([pThis](){
                    ContinuationAsyncTaskFromOneFuture<Func, T>::onInnerFutureCompleted(pThis);
                });
                pThis->m_pAntecessorFuture.reset();
            });
        } else {
            pThis->setException(pThis->m_pAntecessorFuture->getException());
            pThis->m_pAntecessorFuture.reset();
        }
    }

private:
    static void onInnerFutureCompleted(IntrusiveSharedPtr<ContinuationAsyncTaskFromOneFuture<Func, T> > pThis) {
        if(pThis->m_pAsyncOpFuture->isCompletedNormally()) {
            pThis->m_pExecutor->enqueue([pThis]() noexcept {
                pThis->setFromOtherFutureMove(pThis->m_pAsyncOpFuture);
                pThis->m_pAsyncOpFuture.reset();
            });
        } else {
            pThis->setException(pThis->m_pAsyncOpFuture->getException());
            pThis->m_pAsyncOpFuture.reset();
        }
    }

    Executor* m_pExecutor;
    Func m_func;
    IntrusiveSharedPtr<PromiseFuturePair<T> > m_pAntecessorFuture;
    IntrusiveSharedPtr<typename PromiseFuturePair<ReturnType>::ConsumerFacingType> m_pAsyncOpFuture;
};

/** @brief [Internal use] A task that depends on a single future to start executing (can start as soon as that future completes),
will take the value via const reference, and executes an asynchronous operation returning a future. */
template<typename Func>
class ContinuationAsyncTaskFromOneVoidFuture : public PromiseFuturePair<typename std::invoke_result<Func>::type::BaseType> {
public:
    using ReturnFuture = typename std::invoke_result<Func>::type;
    using ReturnType = typename ReturnFuture::BaseType;
    ContinuationAsyncTaskFromOneVoidFuture(Executor* pExecutor, Func func, IntrusiveSharedPtr<PromiseFuturePairBase> pFuture)
        :m_pExecutor(pExecutor),
        m_func(std::move(func)),
        m_pAntecessorFuture(pFuture)
    {
    }

    static void onFutureCompleted(IntrusiveSharedPtr<ContinuationAsyncTaskFromOneVoidFuture<Func> > pThis) {
        if(pThis->m_pAntecessorFuture->isCompletedNormally()) {
            pThis->m_pExecutor->enqueue([pThis]() noexcept {
                pThis->m_pAsyncOpFuture = pThis->m_func().getPromiseFuturePair();
                pThis->m_pAsyncOpFuture->addSynchronousCallback([pThis](){
                    ContinuationAsyncTaskFromOneVoidFuture<Func>::onInnerFutureCompleted(pThis);
                });
                pThis->m_pAntecessorFuture.reset();
            });
        } else {
            pThis->setException(pThis->m_pAntecessorFuture->getException());
            pThis->m_pAntecessorFuture.reset();
        }
    }

private:
    static void onInnerFutureCompleted(IntrusiveSharedPtr<ContinuationAsyncTaskFromOneVoidFuture<Func> > pThis) {
        if(pThis->m_pAsyncOpFuture->isCompletedNormally()) {
            pThis->m_pExecutor->enqueue([pThis]() noexcept {
                pThis->setFromOtherFutureMove(pThis->m_pAsyncOpFuture);
                pThis->m_pAsyncOpFuture.reset();
            });
        } else {
            pThis->setException(pThis->m_pAsyncOpFuture->getException());
            pThis->m_pAsyncOpFuture.reset();
        }
    }

    Executor* m_pExecutor;
    Func m_func;
    IntrusiveSharedPtr<PromiseFuturePairBase> m_pAntecessorFuture;
    IntrusiveSharedPtr<typename PromiseFuturePair<ReturnType>::ConsumerFacingType> m_pAsyncOpFuture;
};

/** @brief [Internal use] A task that executes an asynchronous loop body for as long as a looping condition is true, and completes afterwards */
template<typename T, typename FuncCond, typename FuncBody>
class ContinuationTaskAsyncLoop : public PromiseFuturePair<T> {
public:
    ContinuationTaskAsyncLoop(Executor* pExecutor, FuncCond cond, FuncBody body, Future<T> future)
        :m_pExecutor(pExecutor),
        m_cond(std::move(cond)),
        m_body(std::move(body)),
        m_currentFuture(future)
    {
    }

    static void onFutureCompleted(IntrusiveSharedPtr<ContinuationTaskAsyncLoop<T, FuncCond, FuncBody> > pThis) {
        if(pThis->m_currentFuture.isCompletedNormally()) {
            if(pThis->m_cond(pThis->m_currentFuture.get())) {
                pThis->m_currentFuture = pThis->m_body(pThis->m_currentFuture.get());
                pThis->m_currentFuture.addSynchronousCallback([pThis](){ContinuationTaskAsyncLoop<T, FuncCond, FuncBody>::onFutureCompleted(pThis);});
            } else {
                pThis->setFromOtherFutureMove(pThis->m_currentFuture.getPromiseFuturePair());
                pThis->m_currentFuture.reset();
            }
        } else {
            pThis->setException(pThis->m_currentFuture.getException());
            pThis->m_currentFuture.reset();
        }
    }

private:
    Executor* m_pExecutor;
    FuncCond m_cond;
    FuncBody m_body;
    Future<T> m_currentFuture;
};

template<typename T, typename FuncCond, typename FuncBody> // T must be void, but we leave it as template parameter to delay its instantiation
class ContinuationTaskAsyncLoopVoid : public PromiseFuturePair<T> {
public:
    ContinuationTaskAsyncLoopVoid(Executor* pExecutor, FuncCond cond, FuncBody body, Future<T> future)
        :m_pExecutor(pExecutor),
        m_cond(std::move(cond)),
        m_body(std::move(body)),
        m_currentFuture(future)
    {
    }

    static void onFutureCompleted(IntrusiveSharedPtr<ContinuationTaskAsyncLoopVoid<T, FuncCond, FuncBody> > pThis) {
        if(pThis->m_currentFuture.isCompletedNormally()) {
            if(pThis->m_cond()) {
                pThis->m_currentFuture = pThis->m_body();
                pThis->m_currentFuture.addSynchronousCallback([pThis](){ContinuationTaskAsyncLoopVoid<T, FuncCond, FuncBody>::onFutureCompleted(pThis);});
            } else {
                pThis->setFromOtherFutureMove(pThis->m_currentFuture.getPromiseFuturePair());
                pThis->m_currentFuture.reset();
            }
        } else {
            pThis->setException(pThis->m_currentFuture.getException());
            pThis->m_currentFuture.reset();
        }
    }

private:
    Executor* m_pExecutor;
    FuncCond m_cond;
    FuncBody m_body;
    Future<T> m_currentFuture;
};

/** @brief [Internal use] A task that, when the given future completes, completes moving its value on normal completion,
or calls the given function on the exception. The function is synchronous and returns a @c T.*/
template<typename T, typename Func>
class ContinuationTaskCatchAll : public PromiseFuturePair<T> {
public:
    ContinuationTaskCatchAll(Executor* pExecutor, Func func, Future<T> future)
        :m_pExecutor(pExecutor),
        m_func(std::move(func)),
        m_future(future)
    {
    }

    void onFutureCompleted() {
        if(m_future.isCompletedNormally()) {
            this->setFromOtherFutureMove(m_future.getPromiseFuturePair());
            m_future.reset();
        } else {
            std::exception_ptr pException = m_future.getException();
            m_future.reset();
            m_pExecutor->enqueue([this,pException](){
                this->computeAndSet(std::move(m_func), pException);
            });
        }
    }

private:
    Executor* m_pExecutor;
    Func m_func;
    Future<T> m_future;
};

/** @brief [Internal use] A task that, when the given future completes, completes moving its value on normal completion,
or calls the given function on the exception. The function is asynchronous and returns a @c Future<T>*/
template<typename T, typename Func>
class ContinuationTaskAsyncCatchAll : public PromiseFuturePair<T> {
public:
    ContinuationTaskAsyncCatchAll(Executor* pExecutor, Func func, Future<T> future)
        :m_pExecutor(pExecutor),
        m_func(std::move(func)),
        m_antecessorFuture(future),
        m_exceptionHandlerFuture(nullptr)
    {
    }

    static void onFutureCompleted(IntrusiveSharedPtr<ContinuationTaskAsyncCatchAll<T, Func> > pThis) {
        if(pThis->m_antecessorFuture.isCompletedNormally()) {
            pThis->setFromOtherFutureMove(pThis->m_antecessorFuture.getPromiseFuturePair());
            pThis->m_antecessorFuture.reset();
        } else {
            pThis->m_exceptionHandlerFuture = pThis->m_func(pThis->m_antecessorFuture.getException());
            pThis->m_antecessorFuture.reset();
            pThis->m_exceptionHandlerFuture.addSynchronousCallback([pThis](){pThis->onExceptionHandlerComplete();});
        }
    }

private:
    void onExceptionHandlerComplete() {
        this->setFromOtherFutureMove(m_exceptionHandlerFuture.getPromiseFuturePair());
        m_exceptionHandlerFuture.reset();
    }

    Executor* m_pExecutor;
    Func m_func;
    Future<T> m_antecessorFuture;
    Future<T> m_exceptionHandlerFuture;
};

template<typename R, typename Func, typename... FutureArgs>
class ContinuationTask : public PromiseFuturePair<R> {
public:
    explicit ContinuationTask(Executor* pTp, Func func, FutureArgs... futures)
        :m_pTp(pTp),
        m_remaining(sizeof...(futures)),
        m_func(std::move(func)),
        m_futures(std::move(futures)...)
    {
    }

    static void onFutureCompleted(IntrusiveSharedPtr<ContinuationTask<R, Func, FutureArgs...> > pThis) {
        unsigned old = pThis->m_remaining.fetch_sub(1);
        if (old == 1) {
            pThis->m_pTp->enqueue([pThis]() noexcept {
                pThis->computeAndSetWithTuple(std::move(pThis->m_func), std::move(pThis->m_futures));
            });
        }
    }

    Executor* m_pTp;
    std::atomic_uint m_remaining;
    Func m_func;
    std::tuple<FutureArgs...> m_futures;
};

template<typename R, typename Func, typename Arg>
class ContinuationTaskArray : public PromiseFuturePair<R> {
public:
    explicit ContinuationTaskArray(Executor* pTp, Func func, std::vector<Future<Arg> > futures)
        :m_pTp(pTp),
        m_remaining(futures.size()),
        m_func(std::move(func)),
        m_futures(std::move(futures))
    {
        // empty
    }

    static void attachContinuations(IntrusiveSharedPtr<ContinuationTaskArray<R, Func, Arg> > pThis) {
        for (auto& future : pThis->m_futures) {
            future.addSynchronousCallback([pThis]() {ContinuationTaskArray<R, Func, Arg>::onFutureCompleted(pThis); });
        }
    }

    static void onFutureCompleted(IntrusiveSharedPtr<ContinuationTaskArray<R, Func, Arg> > pThis) {
        unsigned old = pThis->m_remaining.fetch_sub(1);
        if (old == 1) {
            pThis->m_pTp->enqueue([pThis]() noexcept {
                pThis->computeAndSet(std::move(pThis->m_func), std::move(pThis->m_futures));
            });
        }
    }

    Executor* m_pTp;
    std::atomic_uint m_remaining;
    Func m_func;
    std::vector<Future<Arg> > m_futures;
};

template<unsigned k, typename R, typename Func, typename... FutureArgs>
void attachContinuations(IntrusiveSharedPtr<carpal_private::ContinuationTask<R, Func, FutureArgs...> > pTask) {
    std::get<k-1>(pTask->m_futures).addSynchronousCallback([pTask]() {
        carpal_private::ContinuationTask<R, Func, FutureArgs...>::onFutureCompleted(pTask);
    });
    if(k>1) {
        attachContinuations<(k>1 ? k-1 : 1)>(pTask);
    }
}

} // namespace carpal_private

} // namespace carpal
