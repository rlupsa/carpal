// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#pragma once

#include "carpal/config.h"
#include "carpal/IntrusiveSharedPtr.h"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <variant>

#include <assert.h>

namespace carpal {

/** @brief A value to be conveyed through a stream. It can be a regular item, an EOF marker, or an exception.
 *
 * The EOF marker or the exception can only be the last value conveyed through the stream.
 *
 * Terminology:
 *   - StreamValue = an object passed through the channel - can be a regular item, an EOF marker (plus any additional information), or an exception_ptr
 *   - Item = a regular item passed through the channel; must be movable
 *   - Eof = the Eof marker; must be copyable
 * */
template<typename Item, typename Eof=void>
class StreamValue {
public:
    StreamValue()
        :m_val(std::in_place_index_t<0>())
        {}
    static StreamValue<Item,Eof> makeItem(Item el) {
        return StreamValue<Item,Eof>(std::variant<std::nullptr_t,Item,Eof, std::exception_ptr>(std::in_place_index_t<1>(), std::move(el)));
    }
    static StreamValue<Item,Eof> makeEof(Eof eof) {
        return StreamValue<Item,Eof>(std::variant<std::nullptr_t,Item,Eof, std::exception_ptr>(std::in_place_index_t<2>(), std::move(eof)));
    }
    static StreamValue<Item,Eof> makeException(std::exception_ptr ex) {
        return StreamValue<Item,Eof>(std::variant<std::nullptr_t,Item,Eof, std::exception_ptr>(std::in_place_index_t<3>(), std::move(ex)));
    }
    static StreamValue<Item,Eof> makeFrom(StreamValue<Item,Eof>& src) {
        switch(src.m_val.index()) {
        case 1:
            {
                Item tmp = std::move(std::get<1>(src.m_val));
                src.m_val.template emplace<0>();
                return StreamValue<Item,Eof>::makeItem(std::move(tmp));
            }
        case 2:
            return StreamValue<Item,Eof>::makeEof(std::get<2>(src.m_val));
        case 3:
            return StreamValue<Item,Eof>::makeException(std::get<3>(src.m_val));
        default:
            return StreamValue<Item,Eof>();
        }
    }
    bool hasValue() const {
        return m_val.index() != 0;
    }
    bool isItem() const {
        return m_val.index() == 1;
    }
    bool isEof() const {
        return m_val.index() == 2;
    }
    bool isException() const {
        return m_val.index() == 3;
    }
    Item const& item() const {
        return std::get<1>(m_val);
    }
    Item& item() {
        return std::get<1>(m_val);
    }
    Eof const& eof() const {
        return std::get<2>(m_val);
    }
    Eof& eof() {
        return std::get<2>(m_val);
    }
    std::exception_ptr exception() const {
        return std::get<3>(m_val);
    }
private:
    StreamValue(std::variant<std::nullptr_t,Item,Eof, std::exception_ptr> val)
        :m_val(std::move(val))
        {}

    std::variant<std::nullptr_t,Item,Eof,std::exception_ptr> m_val;
};

template<typename Item>
class StreamValue<Item,void> {
public:
    StreamValue()
        :m_val(std::in_place_index_t<0>())
        {}
    static StreamValue<Item,void> makeItem(Item el) {
        return StreamValue(std::variant<std::nullptr_t,Item,std::monostate,std::exception_ptr>(std::in_place_index_t<1>(), std::move(el)));
    }
    static StreamValue<Item,void> makeEof() {
        return StreamValue(std::variant<std::nullptr_t,Item,std::monostate,std::exception_ptr>(std::in_place_index_t<2>(), std::monostate()));
    }
    static StreamValue<Item,void> makeException(std::exception_ptr ex) {
        return StreamValue(std::variant<std::nullptr_t,Item,std::monostate,std::exception_ptr>(std::in_place_index_t<3>(), std::move(ex)));
    }
    static StreamValue<Item,void> makeFrom(StreamValue<Item,void>& src) {
        switch(src.m_val.index()) {
        case 1:
            {
                Item tmp = std::move(std::get<1>(src.m_val));
                src.m_val.template emplace<0>();
                return StreamValue<Item,void>::makeItem(std::move(tmp));
            }
        case 2:
            return StreamValue<Item,void>::makeEof();
        case 3:
            return StreamValue<Item,void>::makeException(std::get<3>(src.m_val));
        default:
            return StreamValue<Item,void>();
        }
    }
    bool hasValue() const {
        return m_val.index() != 0;
    }
    bool isItem() const {
        return m_val.index() == 1;
    }
    bool isEof() const {
        return m_val.index() == 2;
    }
    bool isException() const {
        return m_val.index() == 3;
    }
    Item const& item() const {
        return std::get<1>(m_val);
    }
    Item& item() {
        return std::get<1>(m_val);
    }
    std::exception_ptr exception() const {
        return std::get<3>(m_val);
    }
private:
    StreamValue(std::variant<std::nullptr_t,Item,std::monostate,std::exception_ptr> val)
        :m_val(std::move(val))
        {}

    std::variant<std::nullptr_t,Item,std::monostate,std::exception_ptr> m_val;
};

template<typename Item, typename Eof=void>
class SingleProducerSingleConsumerQueue {
public:
    explicit SingleProducerSingleConsumerQueue(size_t queueSize=1)
        :m_queueSize(queueSize)
    {
        // empty
    }

    virtual ~SingleProducerSingleConsumerQueue() {
        // empty
    }

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
    virtual void dispose() {
        delete this;
    }

    /** @brief Returns true if there is at least an element (or exception or EOF) enqueued.
     *
     * @note Since a single consumer is supposed to exist, if this function returns true, it cannot return false later unless the caller consumes an element.
     * */
    bool isValueAvailable() const {
        std::unique_lock<std::mutex> lck{m_mutex};
        return !m_queue.empty();
    }

    /** @brief Sets a callback to be executed when an element is available. The callback gets executed only once.
     *
     * The callback will be called on the caller thread, if an element is already available, or on the producer thread otherwise.
     * This function does not automatically dequeue the element. The callback may (and should) call dequeue() and may also call canEnqueue() (which is guaranteed to return true).
     * */
    void setOnValueAvailableOnceCallback(std::function<void()> callback) {
        std::unique_lock<std::mutex> lck(m_mutex);
        if(m_queue.empty()) {
            m_valueAvailableCallback = callback;
        } else {
            lck.unlock();
            callback();
        }
    }

    /** @brief Reads and returns the next element.
     * */
    StreamValue<Item,Eof> dequeue() {
        std::unique_lock<std::mutex> lck(m_mutex);
        if(m_queue.empty()) {
            waitForValueAvailable(lck);
        }
        if(m_queue.front().isItem()) {
            StreamValue<Item,Eof> ret = StreamValue<Item,Eof>::makeFrom(m_queue.front());
            m_queue.pop();
            CARPAL_LOG_DEBUG("Dequeued item from queue at ", static_cast<void const*>(this));
            onValueDequeued(lck);
            return ret;
        } else {
            CARPAL_LOG_DEBUG("Dequeued ", (m_queue.front().isEof() ? "eof" : "exception"), " from queue at ", static_cast<void const*>(this));
            return StreamValue<Item,Eof>::makeFrom(m_queue.front());
        }
    }

    /** @brief Returns true if there is at least a slot (for enqueueing an element) available.
     *
     * @note Since a single producer is supposed to exist, if this function returns true, it cannot return false later unless the caller adds an element.
     * */
    bool isSlotAvailable() const {
        std::unique_lock<std::mutex> lck(m_mutex);
        return m_queue.size() < m_queueSize;
    }

    /** @brief Sets a callback to be executed when a slot is available. The callback gets executed only once.
     *
     * The callback will be called on the caller thread, if a slot is already available, or on the consumer thread otherwise.
     * The callback may (and should) call send() and may also call isSlotAvailable() (which is guaranteed to return true).
     * */
    void setOnSlotAvailableOnceCallback(std::function<void()> callback) {
        std::unique_lock<std::mutex> lck(m_mutex);
        if(m_queue.size() >= m_queueSize) {
            m_slotAvailableCallback = callback;
        } else {
            lck.unlock();
            callback();
        }
    }

    /** @brief Enqueues an element. If the element is an Eof or exception, no further send() shall be called.
     * */
    void enqueue(StreamValue<Item,Eof> v) {
        std::unique_lock<std::mutex> lck(m_mutex);
        if(m_queue.size() >= m_queueSize && v.isItem()) {
            std::mutex mtx;
            std::condition_variable cv;
            bool done=false;
            assert(m_slotAvailableCallback == nullptr);
            m_slotAvailableCallback = [&mtx,&cv,&done](){
                std::unique_lock<std::mutex> lckx(mtx);
                done = true;
                cv.notify_all();
            };
            CARPAL_LOG_DEBUG("Waiting for slot on queue at ", static_cast<void const*>(this));
            cv.wait(lck, [&done](){return done;});
        }
        CARPAL_LOG_DEBUG("Enqueueing element of type ",
            (v.isItem() ? "item" : (v.isEof() ? "eof" : "exception")),
            " to queue at ", static_cast<void const*>(this));
        m_queue.push(std::move(v));
        onValueEnqueued(lck);
    }

protected:
    virtual void waitForValueAvailable(std::unique_lock<std::mutex>& lck) {
        std::condition_variable cv;
        bool done=false;
        assert(m_valueAvailableCallback == nullptr);
        m_valueAvailableCallback = [this,&cv,&done](){
            std::unique_lock<std::mutex> lckx(m_mutex);
            done = true;
            cv.notify_all();
        };
        CARPAL_LOG_DEBUG("Waiting for element on queue at ", static_cast<void const*>(this));
        cv.wait(lck, [&done](){return done;});
    }

    void onValueEnqueued(std::unique_lock<std::mutex>& lck) {
        if(m_valueAvailableCallback != nullptr) {
            std::function<void()> callback = std::move(m_valueAvailableCallback);
            m_valueAvailableCallback = nullptr;
            lck.unlock();
            callback();
        }
    }
    void onValueDequeued(std::unique_lock<std::mutex>& lck) {
        if(m_slotAvailableCallback != nullptr) {
            std::function<void()> callback = std::move(m_slotAvailableCallback);
            m_slotAvailableCallback = nullptr;
            lck.unlock();
            callback();
        }
    }

    std::atomic<int64_t> m_refcount{0};
    size_t m_queueSize;
    mutable std::mutex m_mutex;
    std::queue<StreamValue<Item,Eof> > m_queue;
    std::function<void()> m_valueAvailableCallback = nullptr;
    std::function<void()> m_slotAvailableCallback = nullptr;
};

} // namespace carpal
