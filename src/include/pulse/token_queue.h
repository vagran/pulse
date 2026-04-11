#ifndef TOKEN_QUEUE_H
#define TOKEN_QUEUE_H

#include <pulse/task.h>
#include <pulse/port.h>
#include <etl/optional.h>


namespace pulse {

template <etl::integral TCounter>
class TokenQueueAwaiter;


/**
 * @brief A coroutine-friendly token queue for asynchronous producer-consumer synchronization.
 *
 * TokenQueue allows producers to push tokens and consumers to asynchronously wait for tokens using
 * co_await. Each token has an associated value that is returned to the awaiter. The queue has a
 * maximum capacity - excess tokens are discarded if not consumed by awaiters.
 *
 * @tparam TCounter Integral type for token values, defaults to size_t.
 */
template <etl::integral TCounter = size_t>
class TokenQueue {
public:
    /**
     * @param initialValue Initial token value returned for the first awaiter.
     * @param maxTokens Maximal number of pending tokens. Excessive tokens are discarded if not
     *  consumed by awaiters.
     */
    TokenQueue(TCounter maxTokens = 1, TCounter initialValue = 0):
        maxTokens(maxTokens),
        value(initialValue)
    {}

    TokenQueue(const TokenQueue &) = delete;

    ~TokenQueue();

    /** Push the specified number of tokens into the queue. */
    void
    Push(TCounter n = 1);

    /** Wait for next token. co_await returns received token value. */
    TokenQueueAwaiter<TCounter>
    Take();

    TCounter
    Peek() const;

    TokenQueueAwaiter<TCounter>
    operator co_await();

private:
    friend class TokenQueueAwaiter<TCounter>;

    TailedList<TokenQueueAwaiter<TCounter> *> waiters;
    const TCounter maxTokens;
    TCounter value, numTokens = 0;
};


template <etl::integral TCounter>
class TokenQueueAwaiter {
public:
    TokenQueueAwaiter(const TokenQueueAwaiter &) = delete;
    TokenQueueAwaiter(TokenQueueAwaiter &&) = delete;

    TokenQueueAwaiter(TokenQueue<TCounter> &queue):
        queue(queue)
    {}

    ~TokenQueueAwaiter();

    bool
    await_ready();

    bool
    await_suspend(Task::CoroutineHandle handle);

    TCounter
    await_resume() const
    {
        return *result;
    }

    /** @return Obtained token value, nullopt if not ready. */
    etl::optional<TCounter>
    GetResult() const
    {
        return result;
    }

private:
    friend class TokenQueue<TCounter>;
    friend class details::ListDefaultTrait<TokenQueueAwaiter *>;

    TokenQueueAwaiter<TCounter> *next = nullptr;
    TokenQueue<TCounter> &queue;
    etl::optional<TCounter> result;
    Task task;

    void
    Wakeup()
    {
        etl::move(task).Schedule();
    }
};


template <etl::integral TCounter>
TokenQueue<TCounter>::~TokenQueue()
{
    // Cannot use iterator since item->next may become invalid if waiter destructed.
    TokenQueueAwaiter<TCounter> *next = waiters.head;
    while (next) {
        auto waiter = next;
        next = waiter->next;
        Task task = etl::move(waiter->task);
        waiter->task.ReleaseHandle();
    }
}

template <etl::integral TCounter>
void
TokenQueue<TCounter>::Push(TCounter n)
{
    CriticalSection cs;

    while (!waiters.IsEmpty() && (numTokens || n)) {
        TokenQueueAwaiter<TCounter> *w = waiters.PopFirst();
        w->result = value - numTokens;
        if (numTokens) {
            numTokens--;
        } else {
            n--;
            value++;
        }
        w->Wakeup();
    }
    value += n;
    if (numTokens + n >= maxTokens) {
        numTokens = maxTokens;
    } else {
        numTokens += n;
    }
}

template <etl::integral TCounter>
TokenQueueAwaiter<TCounter>
TokenQueue<TCounter>::Take()
{
    return TokenQueueAwaiter<TCounter>(*this);
}

template <etl::integral TCounter>
TCounter
TokenQueue<TCounter>::Peek() const
{
    CriticalSection cs;
    return value - numTokens;
}

template <etl::integral TCounter>
TokenQueueAwaiter<TCounter>
TokenQueue<TCounter>::operator co_await()
{
    return TokenQueueAwaiter<TCounter>(*this);
}


template <etl::integral TCounter>
TokenQueueAwaiter<TCounter>::~TokenQueueAwaiter()
{
    if (task) {
        queue.waiters.Remove(this);
    }
}

template <etl::integral TCounter>
bool
TokenQueueAwaiter<TCounter>::await_ready()
{
    CriticalSection cs;
    if (queue.numTokens == 0) {
        return false;
    }
    result = queue.value - queue.numTokens;
    queue.numTokens--;
    return true;
}

template <etl::integral TCounter>
bool
TokenQueueAwaiter<TCounter>::await_suspend(Task::CoroutineHandle handle)
{
    if (result) {
        return false;
    }
    CriticalSection cs;
    if (queue.numTokens == 0) {
        task = handle;
        queue.waiters.AddLast(this);
        return true;
    }
    result = queue.value - queue.numTokens;
    queue.numTokens--;
    return false;
}

} // namespace pulse

#endif /* TOKEN_QUEUE_H */
