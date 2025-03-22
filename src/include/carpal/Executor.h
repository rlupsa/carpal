// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#pragma once

#include <functional>

namespace carpal {

class Executor {
protected:
    virtual ~Executor() {}
    
public:
    virtual void enqueue(std::function<void()> func) = 0;

    /** @brief Causes a waitFor() called with the same id to return.
     * */
    virtual void markCompleted(const void* id) = 0;

    /** @brief Waits until the condition is ready. Depending on the scheduling policy, it may run other tasks.
     * The condition ready is signaled by a markShouldReturn() call with the same id as the one passed here.
     * */
    virtual void waitFor(const void* id) = 0;

};

Executor* defaultExecutor();

} // namespace carpal
