// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#pragma once

#include "carpal/CoroutineScheduler.h"
#include "carpal/Future.h"

#include <coroutine>
#include <functional>

#include <assert.h>

namespace carpal {

    template<typename T>
    class FutureAwaiter {
    public:
        FutureAwaiter(CoroutineScheduler* pScheduler, Future<T> future)
            :m_pScheduler(pScheduler),
            m_pFuture(future.getPromiseFuturePair())
        {
            // nothing else
        }

        bool await_ready() const {
            return m_pFuture->isComplete();
        }
        void await_suspend(std::coroutine_handle<void> thisHandler) {
            CoroutineScheduler* pScheduler = m_pScheduler;
            m_pFuture->addSynchronousCallback([pScheduler, thisHandler]() {
                pScheduler->markRunnable(thisHandler);
            });
        }
        T await_resume() {
            return m_pFuture->get();
        }
    private:
        CoroutineScheduler* m_pScheduler;
        std::shared_ptr<PromiseFuturePair<T> > m_pFuture;
    };


    /** @brief A coroutine that produces a single value.
     * 
     * The coroutine starts executing immediately on the current thread (eager, not lazy). Depending on the coroutine scheduler, it either starts executing
     * on the caller thread, or on a thread in the scheduler (TODO: how is selected?)
     * The caller gets control back at the latest when the coroutine ends.
     * The caller can get the result by calling get(), which blocks until the coroutine ends. get() returns the coroutine return value.
     * While the result is not available, get() tries to schedule some available coroutine to the current thread.
     * 
     * */
    template<typename T>
    class AsyncCoroutine {
    public:
        class promise_type;
        class Awaiter;

        explicit AsyncCoroutine(std::coroutine_handle<AsyncCoroutine<T>::promise_type> handle)
            :m_handle(handle)
        {
            // nothing else
        }

        AsyncCoroutine(AsyncCoroutine const&) = delete;
        AsyncCoroutine& operator=(AsyncCoroutine const&) = delete;
        AsyncCoroutine(AsyncCoroutine&& src)
            :m_handle(std::move(src.m_handle))
        {
            src.m_handle = nullptr;
        }
        AsyncCoroutine& operator=(AsyncCoroutine&& src) {
            AsyncCoroutine<T> tmp(std::move(src));
            std::swap(m_handle, tmp.m_handle);
        }
        ~AsyncCoroutine() {
            if (m_handle != nullptr) {
                m_handle.destroy();
            }
        }

        T& get();

    private:
        std::coroutine_handle<AsyncCoroutine<T>::promise_type> m_handle;
        std::coroutine_handle<void> m_consumerHandler = nullptr;
    };

    template<typename T>
    class AsyncCoroutine<T>::Awaiter {
    public:
        Awaiter(AsyncCoroutine<T>& asyncGenerator, CoroutineScheduler* pConsumerScheduler);
        bool await_ready() const;
        void await_suspend(std::coroutine_handle<void> consumerHandler);
        T& await_resume();
    private:
        std::coroutine_handle<AsyncCoroutine<T>::promise_type> m_handle;
        AsyncCoroutine<T>::promise_type* m_pPromise;
        CoroutineScheduler* m_pConsumerScheduler;
    };

    template<typename T>
    class AsyncCoroutine<T>::promise_type {
    public:
        promise_type()
            :m_pScheduler(defaultCoroutineScheduler())
        {
            // nothing else
        }

        template<typename... Args>
        explicit promise_type(CoroutineScheduler* pScheduler, Args const&...)
            :m_pScheduler(pScheduler)
        {
            // nothing else
        }

        std::suspend_never initial_suspend() {
            return std::suspend_never();
        }
        std::suspend_always final_suspend() noexcept {
            return std::suspend_always();
        }
        void unhandled_exception() {
            assert(false);
        }

        AsyncCoroutine<T> get_return_object() {
            auto handle = std::coroutine_handle<AsyncCoroutine<T>::promise_type>::from_promise(*this);
            return AsyncCoroutine<T>(handle);
        }

        void return_value(T val) {
            std::unique_lock<std::mutex> lck(m_mutex);
            m_val = std::move(val);
            std::function<void()> callback = std::move(m_callback);
            m_callback = nullptr;
            lck.unlock();
            if(callback != nullptr) {
                callback();
            }
        }

        template<typename R>
        FutureAwaiter<R> await_transform(Future<R> future) {
            return FutureAwaiter<T>(m_pScheduler, future);
        }

        template<typename R>
        AsyncCoroutine<R>::Awaiter await_transform(AsyncCoroutine<R>& asyncGenerator) {
            return typename AsyncCoroutine<R>::Awaiter(asyncGenerator, m_pScheduler);
        }

        void addSynchronousCallback(std::function<void()> callback) {
            std::unique_lock<std::mutex> lck(m_mutex);
            if(m_val.has_value()) {
                lck.unlock();
                callback();
                return;
            }
            if(m_callback == nullptr) {
                m_callback = std::move(callback);
            } else {
                m_callback = [first=std::move(m_callback),second=std::move(callback)](){
                    first();
                    second();
                };
            }
        }

    private:
        friend class AsyncCoroutine<T>;
        friend class AsyncCoroutine<T>::Awaiter;

        CoroutineScheduler* m_pScheduler;
        std::mutex m_mutex;
        std::optional<T> m_val;
        std::function<void()> m_callback = nullptr;
    };

    template<typename T>
    AsyncCoroutine<T>::Awaiter::Awaiter(AsyncCoroutine<T>& asyncGenerator, CoroutineScheduler* pConsumerScheduler)
        :m_handle(asyncGenerator.m_handle),
        m_pPromise(&(asyncGenerator.m_handle.promise())),
        m_pConsumerScheduler(pConsumerScheduler)
    {
    }

    template<typename T>
    bool AsyncCoroutine<T>::Awaiter::await_ready() const {
        return m_pPromise->m_val.has_value();
    }

    template<typename T>
    void AsyncCoroutine<T>::Awaiter::await_suspend(std::coroutine_handle<void> consumerHandler)
    {
        m_pPromise->setHandleToResume(consumerHandler);
    }

    template<typename T>
    T& AsyncCoroutine<T>::Awaiter::await_resume() {
        return m_pPromise->m_val.value();
    }

    template<typename T>
    T& AsyncCoroutine<T>::get() {
        AsyncCoroutine<T>::promise_type& promise(m_handle.promise());
        {
            std::unique_lock<std::mutex> lck(promise.m_mutex);
            if(promise.m_val.has_value()) {
                return promise.m_val.value();
            }
        }
        int dummy;
        promise.addSynchronousCallback([&dummy,pScheduler=promise.m_pScheduler](){
            pScheduler->markCompleted(&dummy);
        });
        promise.m_pScheduler->waitFor(&dummy);
        assert(promise.m_val.has_value());
        return promise.m_val.value();
    }

} // namespace carpal
