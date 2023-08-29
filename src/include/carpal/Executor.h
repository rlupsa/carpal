#pragma once

#include <functional>

namespace carpal {

class Executor {
public:
    virtual ~Executor() {}
    
    virtual void enqueue(std::function<void()> func) = 0;
};

} // namespace carpal
