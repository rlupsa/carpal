// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/Future.h"
#include "carpal/Logger.h"
#include "carpal/ThreadPool.h"

#include <catch2/catch_test_macros.hpp>

#include "TestHelper.h"

using namespace carpal;

namespace testns {
    class IntCellAwaiter;
    class IntCell {
    public:
        void addCallback(std::function<void()> callback) const {
            std::unique_lock<std::mutex> lck(m_mutex);
            if(m_val.has_value()) {
                lck.unlock();
                CARPAL_LOG_DEBUG("Value already available - executing callback");
                callback();
            } else {
                CARPAL_LOG_DEBUG("Enqueueing callback");
                m_callbacks.push_back(callback);
            }
        }
        void setValue(int v) {
            std::unique_lock<std::mutex> lck(m_mutex);
            m_val = v;
            std::vector<std::function<void()> > callbacks;
            std::swap(callbacks, m_callbacks);
            lck.unlock();
            CARPAL_LOG_DEBUG("Executing ", callbacks.size(), " callbacks");
            for(auto& callback : callbacks) {
                callback();
            }
        }
        bool hasValue() const {
            std::unique_lock<std::mutex> lck(m_mutex);
            return m_val.has_value();
        }
        int getValue() const {
            std::unique_lock<std::mutex> lck(m_mutex);
            return m_val.value();
        }
    private:
//        friend class IntCellAwaiter;

        mutable std::mutex m_mutex;
        std::optional<int> m_val;
        mutable std::vector<std::function<void()> > m_callbacks;
    };
    class IntCellAwaiter {
    public:
        IntCellAwaiter(carpal::CoroutineScheduler* pScheduler, IntCell const& cell)
            :m_pScheduler(pScheduler),
            m_pCell(&cell)
        {
            // empty
        }
        bool await_ready() const {
            return m_pCell->hasValue();
        }
        void await_suspend(std::coroutine_handle<void> consumerHandler) {
            CARPAL_LOG_DEBUG("Suspending coroutine ", consumerHandler.address());
            m_pCell->addCallback([pScheduler=m_pScheduler,consumerHandler](){
                CARPAL_LOG_DEBUG("Setting to resume coroutine ", consumerHandler.address());
                pScheduler->markRunnable(consumerHandler, false);
            });
        }
        int await_resume() const {
            return m_pCell->getValue();
        }
    private:
        carpal::CoroutineScheduler* m_pScheduler;
        IntCell const* m_pCell;
    };
    IntCellAwaiter create_awaiter(carpal::CoroutineScheduler* pScheduler, IntCell const& cell) {
        return IntCellAwaiter(pScheduler, cell);
    }
} // namespace testns

carpal::Future<int> coroFunc_customAwaiter(testns::IntCell& cell) {
    int ret = co_await cell;
    co_return ret;
};

TEST_CASE("CustomAwaiter_asyncCoroutine", "[customAwaiter]") {
    testns::IntCell cell;
    auto coro = coroFunc_customAwaiter(cell);
    auto f = executeLater([&cell](){cell.setValue(22);return 0;});
    CHECK(coro.get() == 22);
}
