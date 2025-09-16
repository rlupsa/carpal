// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#pragma once

#include "carpal/config.h"
#include "carpal/IntrusiveSharedPtr.h"
#include "carpal/Future.h"

#ifdef ENABLE_COROUTINES
#include "carpal/CoroutineScheduler.h"
#endif

#include "carpal/SingleProducerConsumerQueue.h"

#include <exception>
#include <functional>
#include <optional>
#include <variant>

#include <assert.h>

namespace carpal {

template<typename Item, typename Eof=void>
class StreamSource;

#ifdef ENABLE_COROUTINES

template<typename Item, typename Eof>
class QueueYieldAwaiter {
public:
    QueueYieldAwaiter(CoroutineScheduler* pScheduler,
            SingleProducerSingleConsumerQueue<Item,Eof>* pQueue,
            Item val)
        :m_pScheduler(pScheduler),
        m_pQueue(pQueue),
        m_val(std::move(val))
    {
        //CARPAL_LOG_DEBUG("Creating QueueYieldAwaiter at ", static_cast<void const*>(this), " for queue at ", static_cast<void const*>(pQueue));
    }
    ~QueueYieldAwaiter() {
        //CARPAL_LOG_DEBUG("Destroying QueueYieldAwaiter at ", static_cast<void const*>(this), " for queue at ", static_cast<void const*>(m_pQueue.ptr()));
    }
    bool await_ready() {
        bool ret = m_pQueue->isSlotAvailable();
        //CARPAL_LOG_DEBUG("QueueYieldAwaiter at ", static_cast<void const*>(this), " await_ready() returning ", ret);
        return ret;
    }
    void await_suspend(std::coroutine_handle<void> thisHandler) {
        //CARPAL_LOG_DEBUG("QueueYieldAwaiter at ", static_cast<void const*>(this), " await_suspend() suspending");
        CoroutineScheduler* pScheduler = m_pScheduler;
        m_pQueue->setOnSlotAvailableOnceCallback([pScheduler, thisHandler]() {
            //CARPAL_LOG_DEBUG("QueueYieldAwaiter marking ready");
            pScheduler->markRunnable(thisHandler, true);
        });
    }
    void await_resume() {
        //CARPAL_LOG_DEBUG("QueueYieldAwaiter at ", static_cast<void const*>(this), " await_resume() resumed");
        m_pQueue->enqueue(StreamValue<Item,Eof>::makeItem(std::move(m_val)));
    }

private:
    CoroutineScheduler* m_pScheduler;
    SingleProducerSingleConsumerQueue<Item,Eof>* m_pQueue;
    Item m_val;
};

template<typename Item, typename Eof>
class SingleProducerSingleConsumerQueueFinalAwaiter {
public:
    explicit SingleProducerSingleConsumerQueueFinalAwaiter(SingleProducerSingleConsumerQueue<Item,Eof>* ptr) noexcept
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
    SingleProducerSingleConsumerQueue<Item,Eof>* m_ptr;
};

template<typename Item, typename Eof>
class StreamSourcePromiseType  : public SingleProducerSingleConsumerQueue<Item,Eof> {
public:
    StreamSourcePromiseType()
        :m_pScheduler(defaultCoroutineScheduler())
    {
        CARPAL_LOG_DEBUG("StreamSource promise object created @", static_cast<void const*>(this),
            " on default scheduler @", static_cast<void const*>(m_pScheduler));
        this->addRef(); // this reference will be removed in final_suspend()
    }

    ~StreamSourcePromiseType() override {
        CARPAL_LOG_DEBUG("StreamSource promise object @", static_cast<void const*>(this), " destroyed");
    }

    void dispose() override {
        m_handle.destroy();
    }

    std::suspend_never initial_suspend() {
        return  std::suspend_never();
    }
    SingleProducerSingleConsumerQueueFinalAwaiter<Item,Eof> final_suspend() noexcept {
        return SingleProducerSingleConsumerQueueFinalAwaiter<Item,Eof>(this);
    }

    void unhandled_exception() {
        this->enqueue(StreamValue<Item,Eof>::makeException(std::current_exception()));
    }

    StreamSource<Item,Eof> get_return_object() {
        this->m_handle = std::coroutine_handle<typename StreamSource<Item,Eof>::promise_type>::from_promise(*this);
        CARPAL_LOG_DEBUG("StreamSource coroutine handle=", m_handle.address(), " created; promise object @", static_cast<void const*>(this));
        return StreamSource<Item,Eof>(IntrusiveSharedPtr<StreamSourcePromiseType<Item,Eof> >(this));
    }

    QueueYieldAwaiter<Item,Eof> yield_value(Item item) {
        return QueueYieldAwaiter<Item,Eof>(m_pScheduler, this, std::move(item));
    }

    void return_value(Eof val) {
        this->enqueue(StreamValue<Item,Eof>::makeEof(std::move(val)));
    }

    SwitchThreadAwaiter await_transform(CoroutineSchedulingInfo const& newSchedulingInfo) {
        CARPAL_LOG_DEBUG("Switching StreamSource coroutine for promise @", static_cast<void const*>(this), " to scheduler @", static_cast<void const*>(newSchedulingInfo.scheduler()));
        m_pScheduler = newSchedulingInfo.scheduler();
        return SwitchThreadAwaiter(newSchedulingInfo);
    }
    SwitchThreadAwaiter await_transform(CoroutineSchedulingInfo&& newSchedulingInfo) {
        CARPAL_LOG_DEBUG("Switching StreamSource coroutine for promise @", static_cast<void const*>(this), " to scheduler @", static_cast<void const*>(newSchedulingInfo.scheduler()));
        m_pScheduler = newSchedulingInfo.scheduler();
        return SwitchThreadAwaiter(newSchedulingInfo);
    }
    SwitchThreadAwaiter await_transform(CoroutineSchedulingInfo& newSchedulingInfo) {
        CARPAL_LOG_DEBUG("Switching StreamSource coroutine for promise @", static_cast<void const*>(this), " to scheduler @", static_cast<void const*>(newSchedulingInfo.scheduler()));
        m_pScheduler = newSchedulingInfo.scheduler();
        return SwitchThreadAwaiter(newSchedulingInfo);
    }

    template<typename What>
    auto await_transform(What&& what) {
        return create_awaiter(m_pScheduler, std::forward<What>(what));
    }

protected:
    void waitForValueAvailable(std::unique_lock<std::mutex>& lck) override {
        int dummy;
        assert(this->m_valueAvailableCallback == nullptr);
        this->m_valueAvailableCallback = [&dummy,this](){
            m_pScheduler->markCompleted(&dummy);
        };
        lck.unlock();
        CARPAL_LOG_DEBUG("Calling scheduler to wait for element on queue at ", static_cast<void const*>(this));
        m_pScheduler->waitFor(&dummy);
        lck.lock();
    }

private:
    CoroutineScheduler* m_pScheduler;
    std::coroutine_handle<typename StreamSource<Item,Eof>::promise_type> m_handle;
};

template<typename Item>
class StreamSourcePromiseType<Item,void> : public SingleProducerSingleConsumerQueue<Item,void> {
public:
    StreamSourcePromiseType()
        :m_pScheduler(defaultCoroutineScheduler())
    {
        CARPAL_LOG_DEBUG("StreamSource promise object created @", static_cast<void const*>(this),
            " on default scheduler @", static_cast<void const*>(m_pScheduler));
        this->addRef(); // this reference will be removed in final_suspend()
    }

    ~StreamSourcePromiseType() override {
        CARPAL_LOG_DEBUG("StreamSource promise object @", static_cast<void const*>(this), " destroyed");
    }

    void dispose() override {
        m_handle.destroy();
    }

    std::suspend_never initial_suspend() {
        return std::suspend_never();
    }

    SingleProducerSingleConsumerQueueFinalAwaiter<Item,void> final_suspend() noexcept {
        return SingleProducerSingleConsumerQueueFinalAwaiter<Item,void>(this);
    }

    void unhandled_exception() {
        CARPAL_LOG_DEBUG("StreamSource promise object @", static_cast<void const*>(this), " unhandled_exception()");
        this->enqueue(StreamValue<Item,void>::makeException(std::current_exception()));
    }

    StreamSource<Item,void> get_return_object() {
        this->m_handle = std::coroutine_handle<typename StreamSource<Item,void>::promise_type>::from_promise(*this);
        CARPAL_LOG_DEBUG("StreamSource coroutine handle=", m_handle.address(), " created; promise object @", static_cast<void const*>(this));
        return StreamSource<Item,void>(IntrusiveSharedPtr<StreamSourcePromiseType<Item,void> >(this));
    }

    QueueYieldAwaiter<Item,void> yield_value(Item item) {
        return QueueYieldAwaiter<Item,void>(m_pScheduler, this, std::move(item));
    }

    void return_void() {
        this->enqueue(StreamValue<Item,void>::makeEof());
    }

    SwitchThreadAwaiter await_transform(CoroutineSchedulingInfo const& newSchedulingInfo) {
        CARPAL_LOG_DEBUG("Switching StreamSource coroutine for promise @", static_cast<void const*>(this), " to scheduler @", static_cast<void const*>(newSchedulingInfo.scheduler()));
        m_pScheduler = newSchedulingInfo.scheduler();
        return SwitchThreadAwaiter(newSchedulingInfo);
    }
    SwitchThreadAwaiter await_transform(CoroutineSchedulingInfo&& newSchedulingInfo) {
        CARPAL_LOG_DEBUG("Switching StreamSource coroutine for promise  @", static_cast<void const*>(this), " to scheduler @", static_cast<void const*>(newSchedulingInfo.scheduler()));
        m_pScheduler = newSchedulingInfo.scheduler();
        return SwitchThreadAwaiter(newSchedulingInfo);
    }
    SwitchThreadAwaiter await_transform(CoroutineSchedulingInfo& newSchedulingInfo) {
        CARPAL_LOG_DEBUG("Switching StreamSource coroutine for promise  @", static_cast<void const*>(this), " to scheduler @", static_cast<void const*>(newSchedulingInfo.scheduler()));
        m_pScheduler = newSchedulingInfo.scheduler();
        return SwitchThreadAwaiter(newSchedulingInfo);
    }

    template<typename What>
    auto await_transform(What&& what) {
        return create_awaiter(m_pScheduler, std::forward<What>(what));
    }

private:
    CoroutineScheduler* m_pScheduler;
    std::coroutine_handle<typename StreamSource<Item,void>::promise_type> m_handle;
};

template<typename Item>
class StreamSourceItemAwaiterTag {
public:
    StreamSourceItemAwaiterTag(IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,void> > pQueue)
        :m_pQueue(pQueue)
    {
        // empty
    }
    IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,void> > queue() const {
        return m_pQueue;
    }
    SingleProducerSingleConsumerQueue<Item,void>* queuePtr() const {
        return m_pQueue.ptr();
    }
private:
    IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,void> > m_pQueue;
};

template<typename Item>
class StreamSourceCoroIterator;

template<typename Item>
class StreamSourceIteratorIncrementAwaiter;

template<typename Item>
class StreamSourceIteratorCreatorAwaiterTag {
public:
    StreamSourceIteratorCreatorAwaiterTag(IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,void> > pQueue)
        :m_pQueue(pQueue)
    {
        // empty
    }
    IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,void> > queue() const {
        return m_pQueue;
    }
private:
    IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,void> > m_pQueue;
};

template<typename Item>
class StreamSourceIteratorIncrementAwaiterTag {
public:
    StreamSourceIteratorIncrementAwaiterTag(StreamSourceCoroIterator<Item>* pIterator)
        :m_pIterator(pIterator)
    {
        // empty
    }
    StreamSourceCoroIterator<Item>* iterator() const {
        return m_pIterator;
    }
private:
    StreamSourceCoroIterator<Item>* m_pIterator;
};

template<typename Item>
class StreamSourceCoroIterator {
public:
    StreamSourceCoroIterator()
        :m_pQueue(nullptr)
    {}
    explicit StreamSourceCoroIterator(IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,void> > pQueue)
        :m_pQueue(pQueue),
        m_value(m_pQueue->dequeue())
    {}
    Item const& operator*() const {
        if(this->m_value.isItem()) {
            return this->m_value.item();
        } else {
            std::rethrow_exception(this->m_value.exception());
        }
    }
    Item& operator*() {
        if(this->m_value.isItem()) {
            return this->m_value.item();
        } else {
            std::rethrow_exception(this->m_value.exception());
        }
    }
    Item const* operator->() const {
        if(&this->m_value.isItem()) {
            return this->m_value.item();
        } else {
            std::rethrow_exception(this->m_value.exception());
        }
    }
    Item* operator->() {
        if(&this->m_value.isItem()) {
            return this->m_value.item();
        } else {
            std::rethrow_exception(this->m_value.exception());
        }
    }
    bool operator==(StreamSourceCoroIterator<Item> const& that) const {
        if(this->m_pQueue != nullptr) {
            if(that.m_pQueue != nullptr) {
                return this->m_pQueue == that.m_pQueue;
            } else {
                return this->m_value.isEof();
            }
        } else {
            if(that.m_pQueue != nullptr) {
                return that.m_value.isEof();
            } else {
                return true;
            }
        }
    }
    bool operator!=(StreamSourceCoroIterator<Item> const& that) const {
        return !(*this == that);
    }
    StreamSourceIteratorIncrementAwaiterTag<Item> operator++() {
        return StreamSourceIteratorIncrementAwaiterTag<Item>(this);
    }

private:
    friend StreamSourceIteratorIncrementAwaiter<Item>;
    IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,void> > m_pQueue;
    StreamValue<Item,void> m_value;
};

#endif // ENABLE_COROUTINES

template<typename Item, typename Eof>
class StreamSource {
public:
#ifdef ENABLE_COROUTINES
    typedef StreamSourcePromiseType<Item,Eof> promise_type;
#endif
    explicit StreamSource(IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,Eof> > q)
        :m_queue(q)
        {}
    StreamSource(StreamSource<Item,Eof> const&) = delete;
    StreamSource(StreamSource<Item,Eof>&&) = default;
    StreamSource& operator=(StreamSource<Item,Eof> const&) = delete;
    StreamSource& operator=(StreamSource<Item,Eof>&&) = default;

    StreamValue<Item,Eof> dequeue() {
        return m_queue->dequeue();
    }

    IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,Eof> > queue() const {
        return m_queue;
    }
    SingleProducerSingleConsumerQueue<Item,Eof>* queuePtr() const {
        return m_queue.ptr();
    }
private:
    IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,Eof> > m_queue;
};

template<typename Item>
class StreamSource<Item,void> {
public:
#ifdef ENABLE_COROUTINES
    typedef StreamSourcePromiseType<Item,void> promise_type;
#endif
    explicit StreamSource(IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,void> > q)
        :m_queue(q)
        {}
    StreamSource(StreamSource<Item,void> const&) = delete;
    StreamSource(StreamSource<Item,void>&&) = default;
    StreamSource& operator=(StreamSource<Item,void> const&) = delete;
    StreamSource& operator=(StreamSource<Item,void>&&) = default;

    StreamValue<Item,void> dequeue() {
        return m_queue->dequeue();
    }

    std::optional<Item> getNextItem() {
        StreamValue<Item,void> value = m_queue->dequeue();
        if(value.isItem()) {
            return std::optional<Item>(std::move(value.item()));
        }
        if(value.isEof()) {
            return std::optional<Item>();
        }
        std::rethrow_exception(value.exception());
    }
#ifdef ENABLE_COROUTINES
    StreamSourceItemAwaiterTag<Item> nextItem() {
        return StreamSourceItemAwaiterTag<Item>(m_queue);
    }

    StreamSourceIteratorCreatorAwaiterTag<Item> begin() {
        return StreamSourceIteratorCreatorAwaiterTag<Item>(m_queue);
    }

    StreamSourceCoroIterator<Item> end() {
        return StreamSourceCoroIterator<Item>();
    }
#endif

    IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,void> > queue() const {
        return m_queue;
    }
    SingleProducerSingleConsumerQueue<Item,void>* queuePtr() const {
        return m_queue.ptr();
    }
private:
    IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,void> > m_queue;
};

#ifdef ENABLE_COROUTINES

/** @brief Awaiter for dequeueing an element from a StreamSource
 * */
template<typename Item, typename Eof>
class StreamSourceElementAwaiter {
public:
    StreamSourceElementAwaiter(CoroutineScheduler* pScheduler, SingleProducerSingleConsumerQueue<Item,Eof>* pQueue)
        :m_pScheduler(pScheduler),
        m_pQueue(pQueue)
        {}
    StreamSourceElementAwaiter(StreamSourceElementAwaiter<Item,Eof> const&) = delete;
    StreamSourceElementAwaiter(StreamSourceElementAwaiter<Item,Eof>&&) = default;
    StreamSourceElementAwaiter& operator=(StreamSourceElementAwaiter<Item,Eof> const&) = delete;
    StreamSourceElementAwaiter& operator=(StreamSourceElementAwaiter<Item,Eof>&&) = default;

    bool await_ready() {
        bool ret = m_pQueue->isValueAvailable();
        //CARPAL_LOG_DEBUG("StreamSourceElementAwaiter at ", static_cast<void const*>(this), " await_ready() returning ", ret);
        return ret;
    }
    void await_suspend(std::coroutine_handle<void> thisHandler) {
        //CARPAL_LOG_DEBUG("StreamSourceElementAwaiter at ", static_cast<void const*>(this), " await_suspend() suspending");
        CoroutineScheduler* pScheduler = m_pScheduler;
        m_pQueue->setOnValueAvailableOnceCallback([pScheduler, thisHandler]() {
            //CARPAL_LOG_DEBUG("StreamSourceElementAwaiter marking ready");
            pScheduler->markRunnable(thisHandler, true);
        });
    }
    StreamValue<Item,Eof> await_resume() {
        //CARPAL_LOG_DEBUG("StreamSourceElementAwaiter at ", static_cast<void const*>(this), " await_resume() resumed");
        return m_pQueue->dequeue();
    }

private:
    CoroutineScheduler* m_pScheduler;
    SingleProducerSingleConsumerQueue<Item,Eof>* m_pQueue;
};

template<typename Item, typename Eof>
StreamSourceElementAwaiter<Item,Eof> create_awaiter(CoroutineScheduler* pScheduler, StreamSource<Item,Eof> const& streamSource) {
    return StreamSourceElementAwaiter<Item,Eof>(pScheduler, streamSource.queuePtr());
}

/** @brief Awaiter for dequeueing an item from a StreamSource<Item,void>
 * */
template<typename Item>
class StreamSourceItemAwaiter {
public:
    StreamSourceItemAwaiter(CoroutineScheduler* pScheduler, SingleProducerSingleConsumerQueue<Item,void>* pQueue)
        :m_pScheduler(pScheduler),
        m_pQueue(pQueue)
        {}
    StreamSourceItemAwaiter(StreamSourceItemAwaiter<Item> const&) = delete;
    StreamSourceItemAwaiter(StreamSourceItemAwaiter<Item>&&) = default;
    StreamSourceItemAwaiter& operator=(StreamSourceItemAwaiter<Item> const&) = delete;
    StreamSourceItemAwaiter& operator=(StreamSourceItemAwaiter<Item>&&) = default;

    bool await_ready() {
        bool ret = m_pQueue->isValueAvailable();
        CARPAL_LOG_DEBUG("StreamSourceItemAwaiter at ", static_cast<void const*>(this), " await_ready() returning ", ret);
        return ret;
    }
    void await_suspend(std::coroutine_handle<void> thisHandler) {
        CARPAL_LOG_DEBUG("StreamSourceItemAwaiter at ", static_cast<void const*>(this), " await_suspend() suspending");
        CoroutineScheduler* pScheduler = m_pScheduler;
        m_pQueue->setOnValueAvailableOnceCallback([pScheduler, thisHandler]() {
            CARPAL_LOG_DEBUG("StreamSourceItemAwaiter marking ready");
            pScheduler->markRunnable(thisHandler, true);
        });
    }
    std::optional<Item> await_resume() {
        CARPAL_LOG_DEBUG("StreamSourceItemAwaiter at ", static_cast<void const*>(this), " await_resume() resumed");
        StreamValue<Item,void> value = m_pQueue->dequeue();
        if(value.isItem()) {
            return std::optional<Item>(std::move(value.item()));
        }
        if(value.isEof()) {
            return std::optional<Item>();
        }
        std::rethrow_exception(value.exception());
    }

private:
    CoroutineScheduler* m_pScheduler;
    SingleProducerSingleConsumerQueue<Item,void>* m_pQueue;
};

template<typename Item>
StreamSourceItemAwaiter<Item> create_awaiter(CoroutineScheduler* pScheduler, StreamSourceItemAwaiterTag<Item> const& streamSource) {
    return StreamSourceItemAwaiter<Item>(pScheduler, streamSource.queuePtr());
}

/** @brief Awaiter for creating an iterator
 * */
template<typename Item>
class StreamSourceIteratorCreatorAwaiter {
public:
    StreamSourceIteratorCreatorAwaiter(CoroutineScheduler* pScheduler, IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,void> > pQueue)
        :m_pScheduler(pScheduler),
        m_pQueue(pQueue)
        {}
    StreamSourceIteratorCreatorAwaiter(StreamSourceIteratorCreatorAwaiter<Item> const&) = delete;
    StreamSourceIteratorCreatorAwaiter(StreamSourceIteratorCreatorAwaiter<Item>&&) = default;
    StreamSourceIteratorCreatorAwaiter& operator=(StreamSourceIteratorCreatorAwaiter<Item> const&) = delete;
    StreamSourceIteratorCreatorAwaiter& operator=(StreamSourceIteratorCreatorAwaiter<Item>&&) = default;

    bool await_ready() {
        bool ret = m_pQueue->isValueAvailable();
        CARPAL_LOG_DEBUG("StreamSourceIteratorCreatorAwaiter at ", static_cast<void const*>(this), " await_ready() returning ", ret);
        return ret;
    }
    void await_suspend(std::coroutine_handle<void> thisHandler) {
        CARPAL_LOG_DEBUG("StreamSourceIteratorCreatorAwaiter at ", static_cast<void const*>(this), " await_suspend() suspending");
        CoroutineScheduler* pScheduler = m_pScheduler;
        m_pQueue->setOnValueAvailableOnceCallback([pScheduler, thisHandler]() {
            CARPAL_LOG_DEBUG("StreamSourceIteratorCreatorAwaiter marking ready");
            pScheduler->markRunnable(thisHandler, true);
        });
    }
    StreamSourceCoroIterator<Item> await_resume() {
        CARPAL_LOG_DEBUG("StreamSourceIteratorCreatorAwaiter at ", static_cast<void const*>(this), " await_resume() resumed");
        return StreamSourceCoroIterator<Item>(m_pQueue);
    }

private:
    CoroutineScheduler* m_pScheduler;
    IntrusiveSharedPtr<SingleProducerSingleConsumerQueue<Item,void> > m_pQueue;
};

template<typename Item>
StreamSourceIteratorCreatorAwaiter<Item> create_awaiter(CoroutineScheduler* pScheduler, StreamSourceIteratorCreatorAwaiterTag<Item> const& streamSource) {
    return StreamSourceIteratorCreatorAwaiter<Item>(pScheduler, streamSource.queue());
}

/** @brief Awaiter for incrementing an iterator
 * */
template<typename Item>
class StreamSourceIteratorIncrementAwaiter {
public:
    StreamSourceIteratorIncrementAwaiter(CoroutineScheduler* pScheduler, StreamSourceCoroIterator<Item>* pIterator)
        :m_pScheduler(pScheduler),
        m_pIterator(pIterator)
        {}
    StreamSourceIteratorIncrementAwaiter(StreamSourceIteratorIncrementAwaiter<Item> const&) = delete;
    StreamSourceIteratorIncrementAwaiter(StreamSourceIteratorIncrementAwaiter<Item>&&) = default;
    StreamSourceIteratorIncrementAwaiter& operator=(StreamSourceIteratorIncrementAwaiter<Item> const&) = delete;
    StreamSourceIteratorIncrementAwaiter& operator=(StreamSourceIteratorIncrementAwaiter<Item>&&) = default;

    bool await_ready() {
        bool ret = m_pIterator->m_pQueue->isValueAvailable();
        CARPAL_LOG_DEBUG("StreamSourceIteratorIncrementAwaiter at ", static_cast<void const*>(this), " await_ready() returning ", ret);
        return ret;
    }
    void await_suspend(std::coroutine_handle<void> thisHandler) {
        CARPAL_LOG_DEBUG("StreamSourceIteratorIncrementAwaiter at ", static_cast<void const*>(this), " await_suspend() suspending");
        CoroutineScheduler* pScheduler = m_pScheduler;
        m_pIterator->m_pQueue->setOnValueAvailableOnceCallback([pScheduler, thisHandler]() {
            CARPAL_LOG_DEBUG("StreamSourceIteratorIncrementAwaiter marking ready");
            pScheduler->markRunnable(thisHandler, true);
        });
    }
    void await_resume() {
        CARPAL_LOG_DEBUG("StreamSourceIteratorIncrementAwaiter at ", static_cast<void const*>(this), " await_resume() resumed");
        m_pIterator->m_value = m_pIterator->m_pQueue->dequeue();
    }

private:
    CoroutineScheduler* m_pScheduler;
    StreamSourceCoroIterator<Item>* m_pIterator;
};

template<typename Item>
StreamSourceIteratorIncrementAwaiter<Item> create_awaiter(CoroutineScheduler* pScheduler, StreamSourceIteratorIncrementAwaiterTag<Item> const& tag) {
    return StreamSourceIteratorIncrementAwaiter<Item>(pScheduler, tag.iterator());
}

#endif // ENABLE_COROUTINES

} // namespace carpal
