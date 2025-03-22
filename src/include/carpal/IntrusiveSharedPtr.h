// Copyright Radu Lupsa 2023-2024
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#pragma once

#include "carpal/config.h"
#include "carpal/Logger.h"

#include <memory>

namespace carpal {
/** @brief A shared pointer that uses a reference counter in the template parameter class.
 * 
 * The class used as template parameter must expose two methods:
 *   - void addRef() that increments the internal reference counter
 *   - void removeRef() that decrements the reference counter and destroys the object if the counter reaches zero
 * */
template<class T>
class IntrusiveSharedPtr {
public:
    IntrusiveSharedPtr(IntrusiveSharedPtr<T> const& src)
        :m_p(src.m_p)
    {
        if(m_p != nullptr) {
            m_p->addRef();
        }
    }

    IntrusiveSharedPtr(IntrusiveSharedPtr<T>&& src)
        :m_p(src.m_p)
    {
        src.m_p = nullptr;
    }

    IntrusiveSharedPtr& operator=(IntrusiveSharedPtr<T> const& src) {
        if(src.m_p != nullptr) {
            src.m_p->addRef();
        }
        if(m_p != nullptr) {
            m_p->removeRef();
        }
        m_p = src.m_p;
        return *this;
    }

    IntrusiveSharedPtr& operator=(IntrusiveSharedPtr<T>&& src) {
        if(&src != this) {
            if(m_p != nullptr) {
                m_p->removeRef();
            }
            m_p = src.m_p;
            src.m_p = nullptr;
        }
        return *this;
    }

    ~IntrusiveSharedPtr() {
        if(m_p != nullptr) {
            m_p->removeRef();
        }
    }

    explicit IntrusiveSharedPtr(T* p=nullptr)
        :m_p(p)
    {
        if(m_p != nullptr) {
            m_p->addRef();
        }
    }

    IntrusiveSharedPtr(std::nullptr_t)
        :m_p(nullptr)
    {}

    template<class S>
    IntrusiveSharedPtr(IntrusiveSharedPtr<S> const& src)
        :m_p(src.m_p)
    {
        if(m_p != nullptr) {
            m_p->addRef();
        }
    }

    template<class S>
    IntrusiveSharedPtr(IntrusiveSharedPtr<S>&& src)
        :m_p(src.m_p)
    {
        src.m_p = nullptr;
    }

    template<class S>
    IntrusiveSharedPtr& operator=(IntrusiveSharedPtr<S> const& src) {
        if(src.m_p != nullptr) {
            src.m_p->addRef();
        }
        if(m_p != nullptr) {
            m_p->removeRef();
        }
        m_p = src.m_p;
    }

    template<class S>
    IntrusiveSharedPtr& operator=(IntrusiveSharedPtr<S>&& src) {
        if(&src != this) {
            if(m_p != nullptr) {
                m_p->removeRef();
            }
            m_p = src.m_p;
            src.m_p = nullptr;
        }
    }

    void swap(IntrusiveSharedPtr<T>& that) {
        std::swap(m_p, that.m_p);
    }

    void reset() {
        if(m_p != nullptr) {
            m_p->removeRef();
        }
        m_p = nullptr;
    }

    T* ptr() const {
        return m_p;
    }
    T* operator->() const {
        return m_p;
    }
    T& operator*() const {
        return *m_p;
    }

    template<class S>
    bool operator==(IntrusiveSharedPtr<S> const& other) const {
        return m_p == other.m_p;
    }
    template<class S>
    bool operator!=(IntrusiveSharedPtr<S> const& other) const {
        return m_p != other.m_p;
    }
    bool operator==(std::nullptr_t) const {
        return m_p == nullptr;
    }
    bool operator!=(std::nullptr_t) const {
        return m_p != nullptr;
    }

private:
    template<typename S> friend class IntrusiveSharedPtr;
    T* m_p;
};

template<class T>
void swap(IntrusiveSharedPtr<T>& a, IntrusiveSharedPtr<T>& b) {
    a.swap(b);
}

} // namespace carpal
