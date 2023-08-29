#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <list>
#include <type_traits>
#include <variant>
#include <vector>

#include "Executor.h"

namespace carpal {

Executor* defaultExecutor();

template<typename T>
class Future;

/** @brief Base class for @see PromiseFuturePair, that signals the completion of an asynchronous process.
*/
class PromiseFuturePairBase {
public:
    using CallbackType = std::function<void()>;

    /** @brief The possible current state of an asynchronous computation
     * */
    enum class State : uint8_t {
        not_completed, ///< @brief Not completed (yet)
        completed_normally, ///< @brief Completed normally (no exception)
        exception ///< @brief Completed with an exception
    };

    virtual ~PromiseFuturePairBase();

    /** @brief Waits (blocking the current thread) until the asynchronous computation completes.*/
    void wait() const noexcept {
        if(m_state != State::not_completed) return;
        std::unique_lock<std::mutex> lck(m_mtx);
        while(m_state == State::not_completed) m_cv.wait(lck);
    }

    /** @brief Returns true if already completed. Does not wait.
     * @note a false result can be outdated by the time the caller can use the result.*/
    bool isComplete() const noexcept {
        return m_state != State::not_completed;
    }

    /** @brief Returns true if the asynchronous computation completed normally (without throwing an exception). Does not wait.
     * @note a false result can be outdated by the time the caller can use the result.*/
    bool isCompletedNormally() const {
        return m_state == State::completed_normally;
    }

    /** @brief Returns true if the asynchronous computation completed by throwing an exception. Does not wait.
     * @note a false result can be outdated by the time the caller can use the result.*/
    bool isException() const {
        return m_state == State::exception;
    }

    /** @brief Waits until the asynchronous computation completes (if not completed yet), then returns the exception (if
     * the asynchronous computation throws) or nullptr (if it completes normally). */
    std::exception_ptr getException() const {
        wait();
        return m_exception;
    }

    /** @brief Adds a callback that will get called when the event is triggered.
    *
    * If the event is already triggered, the callback will be called immediately on the current thread, before this
    * function returns. If the event is not triggered yet, then the callback will be called on the thread
    * calling @see notify().
    *
    * @note The caller must make sure that the function remains valid (no dangling pointers) until it gets
    * executed.
    */
    void addSynchronousCallback(CallbackType callback);

protected:
    /** @brief Marks the computation complete. Must be called exactly once.*/
    void notify(State state);

protected:
    mutable std::mutex m_mtx;
    mutable std::condition_variable m_cv;
    std::atomic<State> m_state{State::not_completed};
    std::exception_ptr m_exception = nullptr;
    CallbackType m_continuations = nullptr;
};

/** @brief A channel by which a consumer can get a value that will be produced by a producer at some
* future time.
*
* @note There is a specialization for @c void because one cannot handle a @c void value. Producers of @c void will have pointers to
* @c PromiseFuturePair<void>, while consumers will have pointers to @c PromiseFuturePairBase because consumers of @c void can use values of
* any type. However, general conversion are not allowed - it is not possible for a consumer of @c long, to use a @c PromiseFuturePair<int>.
*/
template<typename T>
class PromiseFuturePair : public PromiseFuturePairBase {
public:
    using BaseType = T;
    using ConsumerFacingType = PromiseFuturePair<T>;

    /** @brief Waits (blocking the current thread) until the value is available, then returns the value.
     * @note The value is returned by non-const reference, allowing the consumer to move it away. It is user's responsibility
     * to make sure this is not used with multiple consumers. */
    T& get() {
        this->wait();
        if(m_state == State::completed_normally) { 
            return m_val.value();
        } else {
            std::rethrow_exception(m_exception);
        }
    }

    /** @brief Sets the value into the channel. Must be called exactly once.*/
    void set(T val) {
        m_val = std::optional<T>(std::move(val));
        this->notify(State::completed_normally);
    }

    /** @brief Executes the given function and sets the result as the value of the future. If the function ends in exception,
     * it sets the exception into the future.
     * @param func The function to be executed. Must return a type convertible to @c T
     * @param args The arguments to be passed (forwarded) to the function
     *
     * @note This function is provided to simplify generic code that also works with @c PromiseFuturePair<void>
     */
    template<typename Func, typename... Args>
    void computeAndSet(Func&& func, Args&&... args) noexcept {
        try {
            this->set(std::forward<Func>(func)(std::forward<Args>(args)...));
        } catch(...) {
            this->setException(std::current_exception());
        }
    }

    /** @brief Sets the value by moving it from the specified future.
     * 
     * @note This function is provided to simplify generic code that also works with @c PromiseFuturePair<void>
     * */
    void setFromOtherFutureMove(std::shared_ptr<PromiseFuturePair<T> > pF) {
        if(pF->isCompletedNormally()) {
            this->set(std::move(pF->get()));
        } else {
            this->setException(pF->getException());
        }
    }

    /** @brief Executes the function and sets the result as the value of the future.
     * @param func The function to be executed. Must return a type convertible to @c T
     * @param args A tuple whose components shall be passed (forwarded) to the function as independent arguments
     *
     * @note This function is provided to simplify generic code that also works with @c PromiseFuturePair<void>
     */
    template<typename Func, typename... Args>
    void computeAndSetWithTuple(Func func, std::tuple<Args...> args) noexcept {
        try {
            this->set(std::apply(std::move(func), std::move(args)));
        } catch(...) {
            this->setException(std::current_exception());
        }
    }

    /** @brief Completes the future with the specified exception.
     * */
    void setException(std::exception_ptr exception) {
        m_exception = exception;
        notify(State::exception);
    }

private:
    std::optional<T> m_val;
};

/** @brief @c PromiseFuturePair specialization for void
 * */
template<>
class PromiseFuturePair<void> : public PromiseFuturePairBase {
public:
    using BaseType = void;
    using ConsumerFacingType = PromiseFuturePairBase;

    /** @brief Marks the asynchronous operation as completed normally.*/
    void set() {
        this->notify(State::completed_normally);
    }

    /** Executes the specified function with the specified arguments (forwarded), then sets the future as being completed
     * (normally, if the function completes normally, or with the exception thrown by the function).
     * 
     * @note This function is provided to simplify writing generic code.
     * */
    template<typename Func, typename... Args>
    void computeAndSet(Func&& func, Args&&... args) noexcept {
        try {
            std::forward<Func>(func)(std::forward<Args>(args)...);
            this->notify(State::completed_normally);
        } catch(...) {
            this->setException(std::current_exception());
        }
    }

    /** @brief If the given future is completed normally, sets the this future as completed normally; otherwise, sets the this future
     * as completed with the same exception as the one given as an argument.
     * 
     * @note This function is provided to simplify writing generic code.
     * */
    void setFromOtherFutureMove(std::shared_ptr<PromiseFuturePairBase> pF) {
        if(pF->isCompletedNormally()) {
            this->notify(State::completed_normally);
        } else {
            this->setException(pF->getException());
        }
    }

    /** @brief Executes the function and sets the result as the value of the future.
     * @param func The function to be executed. Must return a type convertible to @c T
     * @param args A tuple whose components shall be passed (forwarded) to the function as independent arguments
     *
     * @note This function is provided to simplify writing generic code.
     */
    template<typename Func, typename... Args>
    void computeAndSetWithTuple(Func func, std::tuple<Args...> args) noexcept {
        std::apply(std::move(func), std::move(args));
        this->notify(State::completed_normally);
    }

    /** @brief Completes the future with the specified exception.
     * */
    void setException(std::exception_ptr exception) {
        m_exception = exception;
        notify(State::exception);
    }

};

namespace carpal_private {

/** @brief [Internal use] A task that is ready to start executing (does not depend on other futures) */
template<typename R, typename Func>
class ReadyTask : public PromiseFuturePair<R> {
public:
    explicit ReadyTask(Func func)
        : m_func(std::move(func))
    {}
    void execute() noexcept {
        this->computeAndSet(std::move(m_func));
    }
private:
    Func m_func;
};

/** @brief [Internal use] A task that depends on a single future to start executing (can start as soon as that future completes) */
template<typename R, typename Func, typename T>
class ContinuationTaskFromOneFuture : public PromiseFuturePair<R> {
public:
    ContinuationTaskFromOneFuture(Executor* pExecutor, Func func, Future<T> future)
        :m_pExecutor(pExecutor),
        m_func(std::move(func)),
        m_future(future)
    {
    }

    static void onFutureCompleted(std::shared_ptr<ContinuationTaskFromOneFuture> pThis) {
        if(pThis->m_future.isCompletedNormally()) {
            pThis->m_pExecutor->enqueue([pThis]() noexcept {
                pThis->computeAndSet(std::move(pThis->m_func), pThis->m_future.get());
                pThis->m_future.reset();
            });
        } else {
            pThis->setException(pThis->m_future.getException());
            pThis->m_future.reset();
        }
    }

private:
    Executor* m_pExecutor;
    Func m_func;
    Future<T> m_future;
};

/** @brief [Internal use] A task that depends on a single void future to start executing (can start as soon as that future completes) */
template<typename R, typename Func>
class ContinuationTaskFromOneVoidFuture : public PromiseFuturePair<R> {
public:
    ContinuationTaskFromOneVoidFuture(Executor* pExecutor, Func func, std::shared_ptr<PromiseFuturePairBase > pFuture)
        :m_pExecutor(pExecutor),
        m_func(std::move(func)),
        m_pFuture(pFuture)
    {
    }

    static void onFutureCompleted(std::shared_ptr<ContinuationTaskFromOneVoidFuture> pThis) {
        if(pThis->m_pFuture->isCompletedNormally()) {
            pThis->m_pExecutor->enqueue([pThis]() noexcept {
                pThis->computeAndSet(std::move(pThis->m_func));
                pThis->m_pFuture.reset();
            });
        } else {
            pThis->setException(pThis->m_pFuture->getException());
            pThis->m_pFuture.reset();
        }
    }

private:
    Executor* m_pExecutor;
    Func m_func;
    std::shared_ptr<PromiseFuturePairBase> m_pFuture;
};

/** @brief [Internal use] A task that depends on a single future to start executing (can start as soon as that future completes),
will take the value via const reference, and executes an asynchronous operation returning a future. */
template<typename Func, typename T>
class ContinuationAsyncTaskFromOneFuture : public PromiseFuturePair<typename std::invoke_result<Func,T>::type::BaseType> {
public:
    using ReturnFuture = typename std::invoke_result<Func,T>::type;
    using ReturnType = typename ReturnFuture::BaseType;
    ContinuationAsyncTaskFromOneFuture(Executor* pExecutor, Func func, std::shared_ptr<PromiseFuturePair<T> > pFuture)
        :m_pExecutor(pExecutor),
        m_func(std::move(func)),
        m_pAntecessorFuture(pFuture)
    {
    }

    static void onFutureCompleted(std::shared_ptr<ContinuationAsyncTaskFromOneFuture<Func, T> > pThis) {
        if(pThis->m_pAntecessorFuture->isCompletedNormally()) {
            pThis->m_pExecutor->enqueue([pThis]() noexcept {
                pThis->m_pAsyncOpFuture = pThis->m_func(pThis->m_pAntecessorFuture->get()).getPromiseFuturePair();
                pThis->m_pAsyncOpFuture->addSynchronousCallback([pThis](){
                    ContinuationAsyncTaskFromOneFuture<Func, T>::onInnerFutureCompleted(pThis);
                });
                pThis->m_pAntecessorFuture.reset();
            });
        } else {
            pThis->setException(pThis->m_pAntecessorFuture->getException());
            pThis->m_pAntecessorFuture.reset();
        }
    }

private:
    static void onInnerFutureCompleted(std::shared_ptr<ContinuationAsyncTaskFromOneFuture<Func, T> > pThis) {
        if(pThis->m_pAsyncOpFuture->isCompletedNormally()) {
            pThis->m_pExecutor->enqueue([pThis]() noexcept {
                pThis->setFromOtherFutureMove(pThis->m_pAsyncOpFuture);
                pThis->m_pAsyncOpFuture.reset();
            });
        } else {
            pThis->setException(pThis->m_pAsyncOpFuture->getException());
            pThis->m_pAsyncOpFuture.reset();
        }
    }

    Executor* m_pExecutor;
    Func m_func;
    std::shared_ptr<PromiseFuturePair<T> > m_pAntecessorFuture;
    std::shared_ptr<typename PromiseFuturePair<ReturnType>::ConsumerFacingType> m_pAsyncOpFuture;
};

/** @brief [Internal use] A task that depends on a single future to start executing (can start as soon as that future completes),
will take the value via const reference, and executes an asynchronous operation returning a future. */
template<typename Func>
class ContinuationAsyncTaskFromOneVoidFuture : public PromiseFuturePair<typename std::invoke_result<Func>::type::BaseType> {
public:
    using ReturnFuture = typename std::invoke_result<Func>::type;
    using ReturnType = typename ReturnFuture::BaseType;
    ContinuationAsyncTaskFromOneVoidFuture(Executor* pExecutor, Func func, std::shared_ptr<PromiseFuturePairBase> pFuture)
        :m_pExecutor(pExecutor),
        m_func(std::move(func)),
        m_pAntecessorFuture(pFuture)
    {
    }

    static void onFutureCompleted(std::shared_ptr<ContinuationAsyncTaskFromOneVoidFuture<Func> > pThis) {
        if(pThis->m_pAntecessorFuture->isCompletedNormally()) {
            pThis->m_pExecutor->enqueue([pThis]() noexcept {
                pThis->m_pAsyncOpFuture = pThis->m_func().getPromiseFuturePair();
                pThis->m_pAsyncOpFuture->addSynchronousCallback([pThis](){
                    ContinuationAsyncTaskFromOneVoidFuture<Func>::onInnerFutureCompleted(pThis);
                });
                pThis->m_pAntecessorFuture.reset();
            });
        } else {
            pThis->setException(pThis->m_pAntecessorFuture->getException());
            pThis->m_pAntecessorFuture.reset();
        }
    }

private:
    static void onInnerFutureCompleted(std::shared_ptr<ContinuationAsyncTaskFromOneVoidFuture<Func> > pThis) {
        if(pThis->m_pAsyncOpFuture->isCompletedNormally()) {
            pThis->m_pExecutor->enqueue([pThis]() noexcept {
                pThis->setFromOtherFutureMove(pThis->m_pAsyncOpFuture);
                pThis->m_pAsyncOpFuture.reset();
            });
        } else {
            pThis->setException(pThis->m_pAsyncOpFuture->getException());
            pThis->m_pAsyncOpFuture.reset();
        }
    }

    Executor* m_pExecutor;
    Func m_func;
    std::shared_ptr<PromiseFuturePairBase> m_pAntecessorFuture;
    std::shared_ptr<typename PromiseFuturePair<ReturnType>::ConsumerFacingType> m_pAsyncOpFuture;
};

/** @brief [Internal use] A task that executes an asynchronous loop body for as long as a looping condition is true, and completes afterwards */
template<typename T, typename FuncCond, typename FuncBody>
class ContinuationTaskAsyncLoop : public PromiseFuturePair<T> {
public:
    ContinuationTaskAsyncLoop(Executor* pExecutor, FuncCond cond, FuncBody body, Future<T> future)
        :m_pExecutor(pExecutor),
        m_cond(std::move(cond)),
        m_body(std::move(body)),
        m_currentFuture(future)
    {
    }

    static void onFutureCompleted(std::shared_ptr<ContinuationTaskAsyncLoop<T, FuncCond, FuncBody> > pThis) {
        if(pThis->m_currentFuture.isCompletedNormally()) {
            if(pThis->m_cond(pThis->m_currentFuture.get())) {
                pThis->m_currentFuture = pThis->m_body(pThis->m_currentFuture.get());
                pThis->m_currentFuture.addSynchronousCallback([pThis](){ContinuationTaskAsyncLoop<T, FuncCond, FuncBody>::onFutureCompleted(pThis);});
            } else {
                pThis->setFromOtherFutureMove(pThis->m_currentFuture.getPromiseFuturePair());
                pThis->m_currentFuture.reset();
            }
        } else {
            pThis->setException(pThis->m_currentFuture.getException());
            pThis->m_currentFuture.reset();
        }
    }

private:
    Executor* m_pExecutor;
    FuncCond m_cond;
    FuncBody m_body;
    Future<T> m_currentFuture;
};

template<typename T, typename FuncCond, typename FuncBody> // T must be void, but we leave it as template parameter to delay its instantiation
class ContinuationTaskAsyncLoopVoid : public PromiseFuturePair<T> {
public:
    ContinuationTaskAsyncLoopVoid(Executor* pExecutor, FuncCond cond, FuncBody body, Future<T> future)
        :m_pExecutor(pExecutor),
        m_cond(std::move(cond)),
        m_body(std::move(body)),
        m_currentFuture(future)
    {
    }

    static void onFutureCompleted(std::shared_ptr<ContinuationTaskAsyncLoopVoid<T, FuncCond, FuncBody> > pThis) {
        if(pThis->m_currentFuture.isCompletedNormally()) {
            if(pThis->m_cond()) {
                pThis->m_currentFuture = pThis->m_body();
                pThis->m_currentFuture.addSynchronousCallback([pThis](){ContinuationTaskAsyncLoopVoid<T, FuncCond, FuncBody>::onFutureCompleted(pThis);});
            } else {
                pThis->setFromOtherFutureMove(pThis->m_currentFuture.getPromiseFuturePair());
                pThis->m_currentFuture.reset();
            }
        } else {
            pThis->setException(pThis->m_currentFuture.getException());
            pThis->m_currentFuture.reset();
        }
    }

private:
    Executor* m_pExecutor;
    FuncCond m_cond;
    FuncBody m_body;
    Future<T> m_currentFuture;
};

/** @brief [Internal use] A task that, when the given future completes, completes moving its value on normal completion,
or calls the given function on the exception. The function is synchronous and returns a @c T.*/
template<typename T, typename Func>
class ContinuationTaskCatchAll : public PromiseFuturePair<T> {
public:
    ContinuationTaskCatchAll(Func func, Future<T> future)
        :m_func(std::move(func)),
        m_future(future)
    {
    }

    void onFutureCompleted() {
        if(m_future.isCompletedNormally()) {
            this->setFromOtherFutureMove(m_future.getPromiseFuturePair());
            m_future.reset();
        } else {
            this->computeAndSet(std::move(m_func), m_future.getException());
            m_future.reset();
        }
    }

private:
    Func m_func;
    Future<T> m_future;
};

/** @brief [Internal use] A task that, when the given future completes, completes moving its value on normal completion,
or calls the given function on the exception. The function is asynchronous and returns a @c Future<T>*/
template<typename T, typename Func>
class ContinuationTaskAsyncCatchAll : public PromiseFuturePair<T> {
public:
    ContinuationTaskAsyncCatchAll(Executor* pExecutor, Func func, Future<T> future)
        :m_pExecutor(pExecutor),
        m_func(std::move(func)),
        m_antecessorFuture(future),
        m_exceptionHandlerFuture(nullptr)
    {
    }

    static void onFutureCompleted(std::shared_ptr<ContinuationTaskAsyncCatchAll<T, Func> > pThis) {
        if(pThis->m_antecessorFuture.isCompletedNormally()) {
            pThis->setFromOtherFutureMove(pThis->m_antecessorFuture.getPromiseFuturePair());
            pThis->m_antecessorFuture.reset();
        } else {
            pThis->m_exceptionHandlerFuture = pThis->m_func(pThis->m_antecessorFuture.getException());
            pThis->m_antecessorFuture.reset();
            pThis->m_exceptionHandlerFuture.addSynchronousCallback([pThis](){pThis->onExceptionHandlerComplete();});
        }
    }

private:
    void onExceptionHandlerComplete() {
        this->setFromOtherFutureMove(m_exceptionHandlerFuture.getPromiseFuturePair());
        m_exceptionHandlerFuture.reset();
    }

    Executor* m_pExecutor;
    Func m_func;
    Future<T> m_antecessorFuture;
    Future<T> m_exceptionHandlerFuture;
};

template<typename R, typename Func, typename... FutureArgs>
class ContinuationTask : public PromiseFuturePair<R> {
public:
    explicit ContinuationTask(Executor* pTp, Func func, FutureArgs... futures)
        :m_pTp(pTp),
        m_remaining(sizeof...(futures)),
        m_func(std::move(func)),
        m_futures(std::move(futures)...)
    {
    }

    static void onFutureCompleted(std::shared_ptr<ContinuationTask<R, Func, FutureArgs...> > pThis) {
        unsigned old = pThis->m_remaining.fetch_sub(1);
        if (old == 1) {
            pThis->m_pTp->enqueue([pThis]() noexcept {
                pThis->computeAndSetWithTuple(std::move(pThis->m_func), std::move(pThis->m_futures));
            });
        }
    }

    Executor* m_pTp;
    std::atomic_uint m_remaining;
    Func m_func;
    std::tuple<FutureArgs...> m_futures;
};

template<typename R, typename Func, typename Arg>
class ContinuationTaskArray : public PromiseFuturePair<R> {
public:
    explicit ContinuationTaskArray(Executor* pTp, Func func, std::vector<Future<Arg> > futures)
        :m_pTp(pTp),
        m_remaining(futures.size()),
        m_func(std::move(func)),
        m_futures(std::move(futures))
    {
        // empty
    }

    static void attachContinuations(std::shared_ptr<ContinuationTaskArray<R, Func, Arg> > pThis) {
        for (auto& future : pThis->m_futures) {
            future.addSynchronousCallback([pThis]() {ContinuationTaskArray<R, Func, Arg>::onFutureCompleted(pThis); });
        }
    }

    static void onFutureCompleted(std::shared_ptr<ContinuationTaskArray<R, Func, Arg> > pThis) {
        unsigned old = pThis->m_remaining.fetch_sub(1);
        if (old == 1) {
            pThis->m_pTp->enqueue([pThis]() noexcept {
                pThis->computeAndSet(std::move(pThis->m_func), std::move(pThis->m_futures));
            });
        }
    }

    Executor* m_pTp;
    std::atomic_uint m_remaining;
    Func m_func;
    std::vector<Future<Arg> > m_futures;
};

template<unsigned k, typename R, typename Func, typename... FutureArgs>
void attachContinuations(std::shared_ptr<carpal_private::ContinuationTask<R, Func, FutureArgs...> > pTask) {
    std::get<k-1>(pTask->m_futures).addSynchronousCallback([pTask]() {
        carpal_private::ContinuationTask<R, Func, FutureArgs...>::onFutureCompleted(pTask);
    });
    if(k>1) {
        attachContinuations<(k>1 ? k-1 : 1)>(pTask);
    }
}

} // namespace carpal_private

template<typename T>
Future<T> exceptionFuture(std::exception_ptr ex);

/** @brief The consumer facing side of the promise-future pair.
 * */
template<>
class Future<void> {
public:
    using BaseType = void;

    explicit Future(std::shared_ptr<PromiseFuturePairBase> pf)
        :m_pFuture(std::move(pf))
    {}

    /** @brief Waits (blocking the current thread) until the underlying operation completes.*/
    void wait() const {
        return m_pFuture->wait();
    }

    /** @brief Returns true if already triggered. Does not wait.
     * @note the result can be outdated by the time the caller can use the result.*/
    bool isComplete() const {
        return m_pFuture->isComplete();
    }

    /** @brief Returns true if the future is completed normally - that is, is completed and not with an exception
     * */
    bool isCompletedNormally() const {
        return m_pFuture->isCompletedNormally();
    }

    /** @brief Returns true if the future is completed with exception
     * */
    bool isException() const {
        return m_pFuture->isException();
    }

    /** @brief Waits for the future to complete; then, if completed with exception, returns the exception, otherwise returns nullptr
     * */
    std::exception_ptr getException() const {
        return m_pFuture->getException();
    }

    /** @brief Sets the given function to be executed when the future completes.
     * 
     * If the future is already completed, the callback executes immediately on the current thread;
     * otherwise, it will execute on the thread that completes the future.
     * */
    template<typename Func>
    void addSynchronousCallback(Func func) {
        m_pFuture->addSynchronousCallback(std::move(func));
    }

    std::shared_ptr<PromiseFuturePairBase> getPromiseFuturePair() const {
        return m_pFuture;
    }

    /** @brief Sets the given function to execute, on the given executor, after the current future completes.
     * @return A future that completes with the value (or exception) returned by the given function.
     * */
    template<typename Func>
    Future<typename std::invoke_result<Func>::type>
    then(Executor* pExecutor, Func func) {
        using R = typename std::invoke_result<Func>::type;
        std::shared_ptr<carpal_private::ContinuationTaskFromOneVoidFuture<R, Func> > pRet
            = std::make_shared<carpal_private::ContinuationTaskFromOneVoidFuture<R, Func> >(
            pExecutor, std::move(func), this->getPromiseFuturePair());
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskFromOneVoidFuture<R, Func>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    /** @brief Sets the given function to execute, on the default executor, after the current future completes.
     * @return A future that completes with the value (or exception) returned by the given function.
     * */
    template<typename Func>
    Future<typename std::invoke_result<Func>::type>
    then(Func func) {
        using R = typename std::invoke_result<Func>::type;
        std::shared_ptr<carpal_private::ContinuationTaskFromOneVoidFuture<R, Func> > pRet
            = std::make_shared<carpal_private::ContinuationTaskFromOneVoidFuture<R, Func> >(
            defaultExecutor(), std::move(func), this->getPromiseFuturePair());
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskFromOneVoidFuture<R, Func>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    /** @brief Sets the given asynchronous function to execute, on the given executor, after the current future completes.
     * @return A future that completes when the future returned by @c func completes.
     * */
    template<typename Func>
    Future<typename std::invoke_result<Func>::type::BaseType>
    thenAsync(Executor* pExecutor, Func func) {
        using R = typename std::invoke_result<Func>::type::BaseType;
        std::shared_ptr<carpal_private::ContinuationAsyncTaskFromOneVoidFuture<Func> > pRet
            = std::make_shared<carpal_private::ContinuationAsyncTaskFromOneVoidFuture<Func> >(
            pExecutor, std::move(func), this->getPromiseFuturePair());
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationAsyncTaskFromOneVoidFuture<Func>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    /** @brief Sets the given asynchronous function to execute, on the default executor, after the current future completes.
     * @return A future that completes when the future returned by @c func completes.
     * */
    template<typename Func>
    Future<typename std::invoke_result<Func>::type::BaseType>
    thenAsync(Func func) {
        using R = typename std::invoke_result<Func>::type::BaseType;
        std::shared_ptr<carpal_private::ContinuationAsyncTaskFromOneVoidFuture<Func> > pRet
            = std::make_shared<carpal_private::ContinuationAsyncTaskFromOneVoidFuture<Func> >(
            defaultExecutor(), std::move(func), this->getPromiseFuturePair());
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationAsyncTaskFromOneVoidFuture<Func>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    template<typename FuncCond, typename FuncBody>
    Future<void>
    thenAsyncLoop(Executor* pExecutor, FuncCond cond, FuncBody body) {
        auto pRet = std::make_shared<carpal_private::ContinuationTaskAsyncLoopVoid<void, FuncCond, FuncBody> >(
            pExecutor, std::move(cond), std::move(body), *this);
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncLoopVoid<void, FuncCond, FuncBody>::onFutureCompleted(pRet);});
        return Future<void>(pRet);
    }

    template<typename FuncCond, typename FuncBody>
    Future<void>
    thenAsyncLoop(FuncCond cond, FuncBody body) {
        auto pRet = std::make_shared<carpal_private::ContinuationTaskAsyncLoopVoid<void, FuncCond, FuncBody> >(
            defaultExecutor(), std::move(cond), std::move(body), *this);
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncLoopVoid<void, FuncCond, FuncBody>::onFutureCompleted(pRet);});
        return Future<void>(pRet);
    }

    template<typename Func>
    Future<void> thenCatchAll(Func func) {
        auto pRet = std::make_shared<carpal_private::ContinuationTaskCatchAll<void, Func> >(std::move(func), *this);
        this->addSynchronousCallback([pRet](){pRet->onFutureCompleted();});
        return Future<void>(pRet);
    }

    template<typename Ex, typename Func>
    Future<void> thenCatch(Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> void {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                f(ex);
            }
        };
        auto pRet = std::make_shared<carpal_private::ContinuationTaskCatchAll<void, decltype(generalHandler)> >(
            std::move(generalHandler), *this);
        this->addSynchronousCallback([pRet](){pRet->onFutureCompleted();});
        return Future<void>(pRet);
    }

    template<typename Func>
    Future<void> thenCatchAllAsync(Executor* pExecutor, Func func) {
        auto pRet = std::make_shared<carpal_private::ContinuationTaskAsyncCatchAll<void, Func> >(pExecutor, std::move(func), *this);
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<void, Func>::onFutureCompleted(pRet);});
        return Future<void>(pRet);
    }

    template<typename Func>
    Future<void> thenCatchAllAsync(Func func) {
        auto pRet = std::make_shared<carpal_private::ContinuationTaskAsyncCatchAll<void, Func> >(defaultExecutor(), std::move(func), *this);
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<void, Func>::onFutureCompleted(pRet);});
        return Future<void>(pRet);
    }

    template<typename Ex, typename Func>
    Future<void> thenCatchAsync(Executor* pExecutor, Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> Future<void> {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                return f(ex);
            } catch(...) {
                return exceptionFuture<void>(std::current_exception());
            }
        };
        auto pRet = std::make_shared<carpal_private::ContinuationTaskAsyncCatchAll<void, decltype(generalHandler)> >(pExecutor,
            std::move(generalHandler), *this);
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<void, decltype(generalHandler)>::onFutureCompleted(pRet);});
        return Future<void>(pRet);
    }

    template<typename Ex, typename Func>
    Future<void> thenCatchAsync(Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> Future<void> {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                return f(ex);
            } catch(...) {
                return exceptionFuture<void>(std::current_exception());
            }
        };
        auto pRet = std::make_shared<carpal_private::ContinuationTaskAsyncCatchAll<void, decltype(generalHandler)> >(defaultExecutor(),
            std::move(generalHandler), *this);
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<void, decltype(generalHandler)>::onFutureCompleted(pRet);});
        return Future<void>(pRet);
    }

    /** @brief Resets the pointer to the PromiseFuturePair. Renders the Future unusable for anything but calling the destructor. */
    void reset() {
        m_pFuture.reset();
    }

private:
    std::shared_ptr<PromiseFuturePairBase> m_pFuture;
};

template<typename T>
class Future {
public:
    using BaseType = T;

    explicit Future(std::shared_ptr<PromiseFuturePair<T> > pf)
        :m_pFuture(std::move(pf))
    {}

    /** @brief Waits (blocking the current thread) until the underlying operation completes.*/
    void wait() const {
        return m_pFuture->wait();
    }

    /** @brief Returns true if already triggered. Does not wait.
     * @note the result can be outdated by the time the caller can use the result.*/
    bool isComplete() const {
        return m_pFuture->isComplete();
    }

    bool isCompletedNormally() const {
        return m_pFuture->isCompletedNormally();
    }
    
    bool isException() const {
        return m_pFuture->isException();
    }

    /** @brief Waits (blocking the current thread) until the value is available, then returns the value.*/
    T& get() const {
        return m_pFuture->get();
    }

    std::exception_ptr getException() const {
        return m_pFuture->getException();
    }

    template<typename Func>
    void addSynchronousCallback(Func func) const {
        m_pFuture->addSynchronousCallback(std::move(func));
    }

    operator Future<void>() {
        return Future<void>(m_pFuture);
    }

    std::shared_ptr<PromiseFuturePair<T> > getPromiseFuturePair() const {
        return m_pFuture;
    }

    /** @brief Resets the pointer to the PromiseFuturePair. Renders the Future unusable for anything but calling the destructor.
     * May be called after the Future is used, in order to free any associated resources.*/
    void reset() {
        m_pFuture.reset();
    }

    template<typename Func>
    Future<typename std::invoke_result<Func, T>::type>
    then(Executor* pExecutor, Func func) {
        using R = typename std::invoke_result<Func, T>::type;
        auto pRet = std::make_shared<carpal_private::ContinuationTaskFromOneFuture<R, Func, T> >(
            pExecutor, std::move(func), *this);
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskFromOneFuture<R, Func, T>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    template<typename Func>
    Future<typename std::invoke_result<Func, T>::type>
    then(Func func) {
        using R = typename std::invoke_result<Func, T>::type;
        auto pRet = std::make_shared<carpal_private::ContinuationTaskFromOneFuture<R, Func, T> >(
            defaultExecutor(), std::move(func), *this);
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskFromOneFuture<R, Func, T>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    template<typename Func>
    Future<typename std::invoke_result<Func, T>::type::BaseType>
    thenAsync(Executor* pExecutor, Func func) {
        using R = typename std::invoke_result<Func, T>::type::BaseType;
        auto pRet = std::make_shared<carpal_private::ContinuationAsyncTaskFromOneFuture<Func, T> >(
            pExecutor, std::move(func), this->getPromiseFuturePair());
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationAsyncTaskFromOneFuture<Func, T>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    template<typename Func>
    Future<typename std::invoke_result<Func, T>::type::BaseType>
    thenAsync(Func func) {
        using R = typename std::invoke_result<Func, T>::type::BaseType;
        auto pRet = std::make_shared<carpal_private::ContinuationAsyncTaskFromOneFuture<Func, T> >(
            defaultExecutor(), std::move(func), this->getPromiseFuturePair());
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationAsyncTaskFromOneFuture<Func, T>::onFutureCompleted(pRet);});
        return Future<R>(pRet);
    }

    /** @brief Executes the @c body asynchronous function as long as @c cond returns true, starting on the current future.
     * @param pExecutor Pointer to the executor that will execute the loop body.
     * @param cond The looping condition. Must be defined as @c bool @c cond(T)
     * @param body The loop body. Defined as @c Future<T> @c body(T)
     * @return A future that completes when the @c cond function returns false
     * 
     * When the current future completes, the @c cond function (which must take a @c T and return a @c bool) is executed. If it returns
     * false, the returned future completes with this future value. If @c cond returns true, @c body is executed on the value of the current
     * future. Then, when it completes, the condition is evaluated again on its value and, if true, the body is executed again.
     * 
     * If the current future completes with an exception, or if the body throws an exception or the future it returns completes with an
     * exception, the loop ends and the returned future completes with that exception.
     * */
    template<typename FuncCond, typename FuncBody>
    Future<T>
    thenAsyncLoop(Executor* pExecutor, FuncCond cond, FuncBody body) {
        auto pRet = std::make_shared<carpal_private::ContinuationTaskAsyncLoop<T, FuncCond, FuncBody> >(
            pExecutor, std::move(cond), std::move(body), *this);
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncLoop<T, FuncCond, FuncBody>::onFutureCompleted(pRet);});
        return Future<T>(pRet);
    }

    template<typename FuncCond, typename FuncBody>
    Future<T>
    thenAsyncLoop(FuncCond cond, FuncBody body) {
        auto pRet = std::make_shared<carpal_private::ContinuationTaskAsyncLoop<T, FuncCond, FuncBody> >(
            defaultExecutor(), std::move(cond), std::move(body), *this);
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncLoop<T, FuncCond, FuncBody>::onFutureCompleted(pRet);});
        return Future<T>(pRet);
    }

    template<typename Func>
    Future<T> thenCatchAll(Func func) {
        auto pRet = std::make_shared<carpal_private::ContinuationTaskCatchAll<T, Func> >(std::move(func), *this);
        this->addSynchronousCallback([pRet](){pRet->onFutureCompleted();});
        return Future<T>(pRet);
    }

    template<typename Ex, typename Func>
    Future<T> thenCatch(Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> T {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                return f(ex);
            }
        };
        auto pRet = std::make_shared<carpal_private::ContinuationTaskCatchAll<T, decltype(generalHandler)> >(
            std::move(generalHandler), *this);
        this->addSynchronousCallback([pRet](){pRet->onFutureCompleted();});
        return Future<T>(pRet);
    }

    template<typename Func>
    Future<T> thenCatchAllAsync(Executor* pExecutor, Func func) {
        auto pRet = std::make_shared<carpal_private::ContinuationTaskAsyncCatchAll<T, Func> >(pExecutor, std::move(func), *this);
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<T, Func>::onFutureCompleted(pRet);});
        return Future<T>(pRet);
    }

    template<typename Func>
    Future<T> thenCatchAllAsync(Func func) {
        auto pRet = std::make_shared<carpal_private::ContinuationTaskAsyncCatchAll<T, Func> >(defaultExecutor(), std::move(func), *this);
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<T, Func>::onFutureCompleted(pRet);});
        return Future<T>(pRet);
    }

    template<typename Ex, typename Func>
    Future<T> thenCatchAsync(Executor* pExecutor, Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> Future<T> {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                return f(ex);
            } catch(...) {
                return exceptionFuture<T>(std::current_exception());
            }
        };
        auto pRet = std::make_shared<carpal_private::ContinuationTaskAsyncCatchAll<T, decltype(generalHandler)> >(pExecutor,
            std::move(generalHandler), *this);
        this->addSynchronousCallback([pRet](){
            carpal_private::ContinuationTaskAsyncCatchAll<T, decltype(generalHandler)>::onFutureCompleted(pRet);
        });
        return Future<T>(pRet);
    }

    template<typename Ex, typename Func>
    Future<T> thenCatchAsync(Func func) {
        auto generalHandler = [f=std::move(func)](std::exception_ptr pEx) -> Future<T> {
            try {
                std::rethrow_exception(pEx);
            } catch(Ex& ex) {
                return f(ex);
            } catch(...) {
                return exceptionFuture<T>(std::current_exception());
            }
        };
        auto pRet = std::make_shared<carpal_private::ContinuationTaskAsyncCatchAll<T, decltype(generalHandler)> >(defaultExecutor(),
            std::move(generalHandler), *this);
        this->addSynchronousCallback([pRet](){carpal_private::ContinuationTaskAsyncCatchAll<T, decltype(generalHandler)>::onFutureCompleted(pRet);});
        return Future<T>(pRet);
    }

private:
    std::shared_ptr<PromiseFuturePair<T> > m_pFuture;
};

/** @brief The promise side of a promise-future pair */
template<typename T>
class Promise {
public:
    /** @brief Creates the promise-future pair */
    Promise() :m_pf(std::make_shared<PromiseFuturePair<T> >()) {}

    /** @brief Sets the value into the promise-future pair. Makes the Future side complete.
    *    This function must be called exactly once in the lifetime of the Promise!*/
    void set(T val) const {
        m_pf->set(std::move(val));
    }

    void setException(std::exception_ptr exception) {
        m_pf->setException(exception);
    }

    /** @brief Returns the Future side of the promise-future pair
     */
    Future<T> future() const {
        return Future<T>(m_pf);
    }
private:
    std::shared_ptr<PromiseFuturePair<T> > m_pf;
};

template<>
class Promise<void> {
public:
    /** @brief Creates the promise-future pair */
    Promise() :m_pf(std::make_shared<PromiseFuturePair<void> >()) {}

    /** @brief Makes the Future side complete.
    *    This function must be called exactly once in the lifetime of the Promise!*/
    void set() const {
        m_pf->set();
    }

    void setException(std::exception_ptr exception) {
        m_pf->setException(exception);
    }

    /** @brief Returns the Future side of the promise-future pair
     */
    Future<void> future() const {
        return Future<void>(m_pf);
    }
private:
    std::shared_ptr<PromiseFuturePair<void> > m_pf;
};

/**
 * @brief Starts an asynchrounous computation
 * @warning Someone must keep the returned future and wait on it to complete! Destroying the returned future without waiting on it will lead to undefined behavior!
 * @param tp The thread pool to use for executing the computation
 * @param func The computation to be executed. Must be a function taking no arguments and returning a value of type R
 * @return A future that completes when the function finishes execution, and can be used to obtain the returned value.
 */
template<typename Func>
Future<typename std::invoke_result<Func>::type>
runAsync(Executor* tp, Func func) {
    using R = typename std::invoke_result<Func>::type;
    std::shared_ptr<carpal_private::ReadyTask<R, Func> > pf = std::make_shared<carpal_private::ReadyTask<R, Func> >(std::move(func));
    tp->enqueue([pf](){pf->execute();});
    return Future<R>(pf);
}

/**
 * @brief Starts an asynchrounous computation on the default thread pool
 * @warning Someone must keep the returned future and wait on it to complete! Destroying the returned future without waiting on it will lead to undefined behavior!
 * @param func The computation to be executed. Must be a function taking no arguments and returning a value of type R
 * @return A future that completes when the function finishes execution, and can be used to obtain the returned value.
 */
template<typename Func>
Future<typename std::invoke_result<Func>::type>
runAsync(Func func) {
    return runAsync(defaultExecutor(), std::move(func));
}

namespace carpal_private {

/**
 * @brief Given a completed future start, it executes loopingPredicate on it and, as long as it returns true, it enqueues 
 * @param executor
 * @param loopingPredicate
 * @param loopFunc
 * @param start
 * @param ret
 */
template<typename R, typename LoopFunc, typename PredicateFunc>
void auxLoop(Executor* pExecutor, PredicateFunc loopingPredicate, LoopFunc loopFunc, R const& start, std::shared_ptr<PromiseFuturePair<R> > ret)
{
    if(!loopingPredicate(start)) {
        ret->set(start);
        return;
    }

    try {
        Future<R> tmpResFuture = loopFunc(start);
        tmpResFuture.addSynchronousCallback([pExecutor,loopingPredicate,loopFunc,tmpResFuture,ret](){
            pExecutor->enqueue([pExecutor,loopingPredicate,loopFunc,tmpResFuture,ret](){
                if(tmpResFuture.isException()) {
                    ret->setException(tmpResFuture.getException());
                } else {
                    auxLoop(pExecutor, loopingPredicate, loopFunc, tmpResFuture.get(), ret);
                }
            });
        });
    } catch(...) {
        ret->setException(std::current_exception());
    }
}
} // namespace carpal_private

/**
 * @brief Arranges that the given function executes when all pre-requisites are available. This version takes a function that takes the actual values as arguments.
 * @warning Someone must keep the returned future and wait on it to complete! Destroying the returned future without waiting on it will lead to undefined behavior!
 * @param pTp The thread pool to use for executing the computation
 * @param func The computation to be executed. Must be a function taking arguments corresponding to the pre-requisites (the values returned by the futures) and returning a value of type R
 * @param futures The futures that represent the pre-requisites of the computation. The computation will not start before they are all complete.
 * @return A future that completes when the function finishes execution, and can be used to obtain the returned value.
 * 
 * @note This version has the function representing the computation take the actual values. This means that this cannot be used if some of the futures are Future<void>
 * @see whenAllFromFutures()
 */
template<typename Func, typename... T>
Future<typename std::invoke_result<Func, T&...>::type>
whenAll(Executor* pTp, Func func, Future<T>... futures) {
    using R = typename std::invoke_result<Func, T&...>::type;
    auto fwdFunc = [func](Future<T>... ff) -> R {return func(ff.get()...);};
    std::shared_ptr<carpal_private::ContinuationTask<R, decltype(fwdFunc), Future<T>...> > pRet
        = std::make_shared<carpal_private::ContinuationTask<R, decltype(fwdFunc), Future<T>...> >(
        pTp, fwdFunc, futures...);
    carpal_private::attachContinuations<sizeof...(T)>(pRet);
    return Future<R>(pRet);
}

/**
 * @brief Arranges that the given function executes when all pre-requisites are available. This version takes a function that takes the actual values as arguments.
 * @warning Someone must keep the returned future and wait on it to complete! Destroying the returned future without waiting on it will lead to undefined behavior!
 * @param func The computation to be executed. Must be a function taking arguments corresponding to the pre-requisites (the values returned by the futures) and returning a value of type R
 * @param futures The futures that represent the pre-requisites of the computation. The computation will not start before they are all complete.
 * @return A future that completes when the function finishes execution, and can be used to obtain the returned value.
 * 
 * @note This version has the function representing the computation take the actual values. This means that this cannot be used if some of the futures are Future<void>
 * @see whenAllFromFutures()
 */
template<typename Func, typename... T>
Future<typename std::invoke_result<Func, T&...>::type>
whenAll(Func func, Future<T>... futures) {
    using R = typename std::invoke_result<Func, T&...>::type;
    auto fwdFunc = [func](Future<T>... ff) -> R {return func(ff.get()...);};
    std::shared_ptr<carpal_private::ContinuationTask<R, decltype(fwdFunc), Future<T>...> > pRet
        = std::make_shared<carpal_private::ContinuationTask<R, decltype(fwdFunc), Future<T>...> >(
        defaultExecutor(), fwdFunc, futures...);
    carpal_private::attachContinuations<sizeof...(T)>(pRet);
    return Future<R>(pRet);
}

/**
 * @brief Arranges that the given function executes when all pre-requisites are available. This version takes a function that takes futures as arguments.
 * @warning Someone must keep the returned future and wait on it to complete! Destroying the returned future without waiting on it will lead to undefined behavior!
 * @param pTp The thread pool to use for executing the computation
 * @param func The computation to be executed. Must be a function taking arguments of types Future<...> corresponding to the pre-requisites and returning a value of type R
 * @param futures The futures that represent the pre-requisites of the computation. The computation will not start before they are all complete.
 * @return A future that completes when the function finishes execution, and can be used to obtain the returned value.
 * 
 * @see addContinuation()
 */
template<typename Func, typename... T>
Future<typename std::invoke_result<Func, Future<T>...>::type>
whenAllFromFutures(Executor* pTp, Func func, Future<T>... futures) {
    using R = typename std::invoke_result<Func, Future<T>...>::type;
    std::shared_ptr<carpal_private::ContinuationTask<R, Func, Future<T>...> > pRet
        = std::make_shared<carpal_private::ContinuationTask<R, Func, Future<T>...> >(pTp, func, futures...);
    carpal_private::attachContinuations<sizeof...(T)>(pRet);
    return Future<R>(pRet);
}

/**
 * @brief Arranges that the given function executes when all pre-requisites are available. This version takes a function that takes futures as arguments.
 * @warning Someone must keep the returned future and wait on it to complete! Destroying the returned future without waiting on it will lead to undefined behavior!
 * @param pTp The thread pool to use for executing the computation
 * @param func The computation to be executed. Must be a function taking arguments of types Future<...> corresponding to the pre-requisites and returning a value of type R
 * @param futures The futures that represent the pre-requisites of the computation. The computation will not start before they are all complete.
 * @return A future that completes when the function finishes execution, and can be used to obtain the returned value.
 * 
 * @see addContinuation()
 */
template<typename Func, typename... T>
Future<typename std::invoke_result<Func, Future<T>...>::type>
whenAllFromFutures(Func func, Future<T>... futures) {
    using R = typename std::invoke_result<Func, Future<T>...>::type;
    std::shared_ptr<carpal_private::ContinuationTask<R, Func, Future<T>...> > pRet
        = std::make_shared<carpal_private::ContinuationTask<R, Func, Future<T>...> >(defaultExecutor(), func, futures...);
    carpal_private::attachContinuations<sizeof...(T)>(pRet);
    return Future<R>(pRet);
}

template<typename Func, typename T>
Future<typename std::invoke_result<Func, std::vector<Future<T> > >::type>
whenAllFromArrayOfFutures(Executor* pTp, Func func, std::vector<Future<T> > futures) {
    using R = typename std::invoke_result<Func, std::vector<Future<T> > >::type;
    std::shared_ptr<carpal_private::ContinuationTaskArray<R, Func, T> > pRet
        = std::make_shared<carpal_private::ContinuationTaskArray<R, Func, T> >(pTp, std::move(func), std::move(futures));
    carpal_private::ContinuationTaskArray<R, Func, T>::attachContinuations(pRet);
    return Future<R>(pRet);
}

template<typename Func, typename T>
Future<typename std::invoke_result<Func, std::vector<Future<T> > >::type>
whenAllFromArrayOfFutures(Func func, std::vector<Future<T> > futures) {
    using R = typename std::invoke_result<Func, std::vector<Future<T> > >::type;
    std::shared_ptr<carpal_private::ContinuationTaskArray<R, Func, T> > pRet
        = std::make_shared<carpal_private::ContinuationTaskArray<R, Func, T> >(defaultExecutor(), std::move(func), std::move(futures));
    carpal_private::ContinuationTaskArray<R, Func, T>::attachContinuations(pRet);
    return Future<R>(pRet);
}

/**
 * @brief A function that returns an already completed future, with the provided value.
 */
template<typename T>
Future<T> completedFuture(T val) {
    Promise<T> p;
    p.set(std::move(val));
    return p.future();
}

/**
 * @brief A function that returns an already completed future of type void.
 */
inline
Future<void> completedFuture() {
    Promise<void> p;
    p.set();
    return p.future();
}

/**
 * @brief A function that returns a future completed with exception
 */
template<typename T>
inline
Future<T> exceptionFuture(std::exception_ptr ex) {
    Promise<T> p;
    p.setException(ex);
    return p.future();
}

/**
 * @brief Adds a loop of an asynchronous function as a continuation to a future
 * @param executor An executor that will execute the continuations
 * @param loopingPredicate A function taking a value of type R and returns true if the loop should execute (again) or false if the value is to be returned
 * @param loopFunc A function taking an R and returning a Future<R> that is to be used as the loop body
 * @param start 
 * @return 
 * 
 * When start completes, the loopingPredicate(start) is invoked. If this returns false, the value in start is set in
 * the retuned future. Otherwise, loopFunc(start) is invoked and its result is treated as if it were start
 */
template<typename R, typename LoopFunc, typename PredicateFunc>
Future<R> executeAsyncLoop(Executor* pExecutor, PredicateFunc loopingPredicate, LoopFunc loopFunc, R const& start)
{
    std::shared_ptr<PromiseFuturePair<R> > ret = std::make_shared<PromiseFuturePair<R> >();
    carpal_private::auxLoop(pExecutor, loopingPredicate, loopFunc, start, ret);
    return Future<R>(ret);
}

class FutureWaiter {
public:
    FutureWaiter();
    ~FutureWaiter();
    void add(Future<void> future);
    void waitAll();

private:
    void onFutureComplete(std::list<Future<void> >::iterator );

    std::mutex m_mtx;
    std::condition_variable m_cond;
    std::list<Future<void> > m_futures;
};

} // namespace carpal
