#pragma once

#include "carpal/SingleProducerConsumerQueue.h"
#include "carpal/Logger.h"

#include <coroutine>

struct EofMark{};

class GeneratorYieldAwaiter {
public:
    explicit GeneratorYieldAwaiter(std::coroutine_handle<void>* pHandler)
        :m_pHandler(pHandler)
    {
        // empty
    }
    bool await_ready() const {
        return false;
    }
    void await_suspend(std::coroutine_handle<void> caller) {
        *m_pHandler = caller;
    }
    void await_resume() {
        // empty
    }
private:
    std::coroutine_handle<void>* m_pHandler;
};

template<typename Item, typename Eof=void>
class Generator;

template<typename Item, typename Eof>
class GeneratorPromise;

template<typename Item, typename Eof>
class GeneratorValueAwaiter;

template<typename Item, typename Eof>
class GeneratorValueAwaiter {
public:
    GeneratorValueAwaiter(Generator<Item,Eof>& sourceCoro, std::coroutine_handle<void> callerHandle, std::coroutine_handle<void>* pNextHandle)
        :m_pSource(sourceCoro.m_pPromise),
        m_callerHandle(callerHandle),
        m_pNextHandle(pNextHandle)
        {}

    bool await_ready() const;
    void await_suspend(std::coroutine_handle<void> caller);
    carpal::StreamValue<Item,Eof> await_resume();

private:
    GeneratorPromise<Item,Eof>* m_pSource;
    std::coroutine_handle<void> m_callerHandle = nullptr;
    std::coroutine_handle<void>* m_pNextHandle = nullptr;
};

template<typename Item, typename Eof>
class GeneratorPromise {
public:
    std::suspend_always initial_suspend() {
        CARPAL_LOG_DEBUG("Initial suspend of Generator with handle ", m_ownHandle.address());
        return std::suspend_always();
    }
    std::suspend_always yield_value(Item item) {
        m_value = carpal::StreamValue<Item,Eof>::makeItem(std::move(item));
        *m_pNextHandle = m_callerHandle;
        CARPAL_LOG_DEBUG("Yield suspend of Generator with handle ", m_ownHandle.address(), "; going to caller handle ", m_callerHandle.address());
        return std::suspend_always();
    }
    void return_value(Eof eof) {
        m_value = carpal::StreamValue<Item,Eof>::makeEof(std::move(eof));
    }
    void unhandled_exception() {
        m_value = carpal::StreamValue<Item,Eof>::makeException(std::current_exception());
    }
    std::suspend_always final_suspend() noexcept {
        *m_pNextHandle = m_callerHandle;
        CARPAL_LOG_DEBUG("Final suspend of Generator with handle ", m_ownHandle.address(), "; going to caller handle ", m_callerHandle.address());
        return std::suspend_always();
    }
    Generator<Item,Eof> get_return_object() {
        this->m_ownHandle = std::coroutine_handle<typename Generator<Item,Eof>::promise_type>::from_promise(*this);
        return Generator<Item,Eof>(this);
    }

    template<typename ThatItem, typename ThatEof>
    GeneratorValueAwaiter<ThatItem,ThatEof> await_transform(Generator<ThatItem,ThatEof>& generator) {
        return GeneratorValueAwaiter<ThatItem,ThatEof>(generator, m_callerHandle, m_pNextHandle);
    }

private:
    friend class Generator<Item, Eof>;
    friend class GeneratorValueAwaiter<Item, Eof>;

    carpal::StreamValue<Item,Eof> m_value;
    std::coroutine_handle<GeneratorPromise<Item,Eof> > m_ownHandle;
    std::coroutine_handle<void> m_callerHandle = nullptr;
    std::coroutine_handle<void>* m_pNextHandle = nullptr;
};

template<typename Item, typename Eof>
class Generator {
public:
    typedef GeneratorPromise<Item,Eof> promise_type;

    explicit Generator(promise_type* pPromise)
        :m_pPromise(pPromise)
        {
            CARPAL_LOG_DEBUG("Generator created @", static_cast<void const*>(this), ", coroutine promise @", static_cast<void const*>(m_pPromise), ", handle ", m_pPromise->m_ownHandle.address());
        }
    Generator(Generator const&) = delete;
    Generator(Generator&& src)
        :m_pPromise(src.m_pPromise)
    {
        CARPAL_LOG_DEBUG("Generator moved from @", static_cast<void const*>(src), ", to @", static_cast<void const*>(this), ", handle ", m_pPromise->m_ownHandle.address());
        src.m_pPromise = nullptr;
    }
    Generator& operator=(Generator const&) = delete;
    Generator& operator=(Generator&& src) {
        if(this != &src) {
            if(m_pPromise != nullptr) {
                CARPAL_LOG_DEBUG("Destroying coroutine handle ", m_pPromise->m_ownHandle.address(), ", Generator @", static_cast<void const*>(this));
                m_pPromise->m_ownHandle.destroy();
            }
            m_pPromise = src.m_pPromise;
            src.m_pPromise = nullptr;
            CARPAL_LOG_DEBUG("Generator moved from @", static_cast<void const*>(src), ", to @", static_cast<void const*>(this), ", handle ", m_pPromise->m_ownHandle.address());
        }
        return *this;
    }
    ~Generator() {
        if(m_pPromise != nullptr) {
            CARPAL_LOG_DEBUG("Destroying coroutine handle ", m_pPromise->m_ownHandle.address(), ", Generator @", static_cast<void const*>(this));
            m_pPromise->m_ownHandle.destroy();
        }
    }

    carpal::StreamValue<Item,Eof> dequeue() {
        CARPAL_LOG_DEBUG("Called dequeue() on coroutine handle ", m_pPromise->m_ownHandle.address());
        std::coroutine_handle<void> nextHandle = m_pPromise->m_ownHandle;
        m_pPromise->m_callerHandle = nullptr;
        m_pPromise->m_pNextHandle = &nextHandle;
        while(!m_pPromise->m_value.hasValue()) {
            CARPAL_LOG_DEBUG("Resuming coroutine handle ", nextHandle.address());
            nextHandle.resume();
        }
        CARPAL_LOG_DEBUG("Finishing dequeue() on coroutine handle ", m_pPromise->m_ownHandle.address());
        return carpal::StreamValue<Item,Eof>::makeFrom(m_pPromise->m_value);
    }
    std::coroutine_handle<void> ownHandle() const {
        return m_pPromise->m_ownHandle;
    }
private:
    friend class GeneratorValueAwaiter<Item, Eof>;

    promise_type* m_pPromise;
};

template<typename Item, typename Eof>
bool GeneratorValueAwaiter<Item,Eof>::await_ready() const {
    return m_pSource->m_value.hasValue();
}

template<typename Item, typename Eof>
void GeneratorValueAwaiter<Item,Eof>::await_suspend(std::coroutine_handle<void> caller) {
    CARPAL_LOG_DEBUG("Generator with handle ", caller.address(), " awaiting for source coroutine handle ", m_pSource->m_ownHandle.address());
    m_pSource->m_callerHandle = caller;
    m_pSource->m_pNextHandle = m_pNextHandle;
    *m_pNextHandle = m_pSource->m_ownHandle;
}

template<typename Item, typename Eof>
carpal::StreamValue<Item,Eof> GeneratorValueAwaiter<Item,Eof>::await_resume() {
    return carpal::StreamValue<Item,Eof>::makeFrom(m_pSource->m_value);
}

template<typename Item>
class Generator<Item,void> {
public:
private:
};
