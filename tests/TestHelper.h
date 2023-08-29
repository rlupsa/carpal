#pragma once

#include "carpal/Future.h"

inline
void delay(unsigned milliseconds = 10) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

template<typename T>
carpal::Future<T> completeLater(T val, unsigned delayMilliseconds = 10) {
    carpal::Promise<T> ret;
    carpal::defaultExecutor()->enqueue([ret,val,delayMilliseconds](){
        delay(delayMilliseconds);
        ret.set(val);
    });
    return ret.future();
}

inline
carpal::Future<void> completeLaterVoid(unsigned delayMilliseconds = 10) {
    carpal::Promise<void> ret;
    carpal::defaultExecutor()->enqueue([ret,delayMilliseconds](){
        delay(delayMilliseconds);
        ret.set();
    });
    return ret.future();
}

template<typename Func>
carpal::Future<typename std::invoke_result<Func>::type> executeLater(Func func, unsigned delayMilliseconds = 10) {
    carpal::Promise<typename std::invoke_result<Func>::type> ret;
    carpal::defaultExecutor()->enqueue([ret,f=std::move(func),delayMilliseconds](){
        delay(delayMilliseconds);
        ret.set(f());
    });
    return ret.future();
}

template<typename Func>
carpal::Future<typename std::invoke_result<Func>::type> executeLaterVoid(Func func, unsigned delayMilliseconds = 10) {
    carpal::Promise<void> ret;
    carpal::defaultExecutor()->enqueue([ret,f=std::move(func),delayMilliseconds](){
        delay(delayMilliseconds);
        f();
        ret.set();
    });
    return ret.future();
}

class NonCopyableInt {
public:
    explicit NonCopyableInt(int v=-1)
        :m_val(v)
        {}
    NonCopyableInt(NonCopyableInt&& src)
        :m_val(src.m_val)
    {
        src.m_val = -1;
    }
    NonCopyableInt& operator=(NonCopyableInt&& src)
    {
        if(this == &src) return *this;
        m_val = src.m_val;
        src.m_val = -1;
        return *this;
    }
    NonCopyableInt(NonCopyableInt const& src) = delete;
    NonCopyableInt& operator=(NonCopyableInt const& src) = delete;

    int val() const {
        return m_val;
    }
private:
    int m_val;
};
