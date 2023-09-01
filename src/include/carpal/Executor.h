// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#pragma once

#include <functional>

namespace carpal {

class Executor {
public:
    virtual ~Executor() {}
    
    virtual void enqueue(std::function<void()> func) = 0;
};

} // namespace carpal
