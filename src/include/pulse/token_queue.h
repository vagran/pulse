#ifndef TOKEN_QUEUE_H
#define TOKEN_QUEUE_H

#include <pulse/task.h>
#include <pulse/port.h>
#include <etl/optional.h>


namespace pulse {

template <class TQueue>
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

    /** Push the specified number of tokens into the queue. Can be called from ISR. */
    void
    Push(TCounter n = 1);

    /** Wait for next token. co_await returns received token value. */
    TokenQueueAwaiter<TokenQueue>
    Take();

    TCounter
    Peek() const;

    TokenQueueAwaiter<TokenQueue>
    operator co_await();

private:
    struct AwaiterSourceTrait;
    using TAwaiterBase = details::AwaiterBase<TCounter, TokenQueue, AwaiterSourceTrait>;

    friend class TokenQueueAwaiter<TokenQueue>;

    TailedList<TAwaiterBase *> waiters;
    const TCounter maxTokens;
    TCounter value, numTokens = 0;

    struct AwaiterSourceTrait {
        static void
        DequeueAwaiter(TokenQueue *queue, TAwaiterBase *awaiter)
        {
            queue->waiters.Remove(awaiter);
        }
    };
};


template <class TQueue>
class TokenQueueAwaiter: public TQueue::TAwaiterBase {
public:
    bool
    await_suspend(tasks::CoroutineHandle handle);

private:
    friend TQueue;
    using Base = TQueue::TAwaiterBase;

    using Base::Base;
};


template <etl::integral TCounter>
TokenQueue<TCounter>::~TokenQueue()
{
    CriticalSection cs;
    for (auto waiter: waiters) {
        if (!waiter->Wakeup()) {
            continue;
        }
        // Return default-constructed value.
        waiter->SetResult();
    }
}

template <etl::integral TCounter>
void
TokenQueue<TCounter>::Push(TCounter n)
{
    CriticalSection cs;

    while (!waiters.IsEmpty() && (numTokens || n)) {
        TAwaiterBase *w = waiters.PopFirst();
        if (!w->Wakeup()) {
            continue;
        }
        w->SetResult(value - numTokens);
        if (numTokens) {
            numTokens--;
        } else {
            n--;
            value++;
        }
    }
    value += n;
    if (numTokens + n >= maxTokens) {
        numTokens = maxTokens;
    } else {
        numTokens += n;
    }
}

template <etl::integral TCounter>
TokenQueueAwaiter<TokenQueue<TCounter>>
TokenQueue<TCounter>::Take()
{
    CriticalSection cs;
    if (numTokens) {
        TCounter result = value - numTokens;
        numTokens--;
        return TokenQueueAwaiter<TokenQueue<TCounter>>(result);
    }
    return TokenQueueAwaiter<TokenQueue<TCounter>>(this);
}

template <etl::integral TCounter>
TCounter
TokenQueue<TCounter>::Peek() const
{
    CriticalSection cs;
    return value - numTokens;
}

template <etl::integral TCounter>
TokenQueueAwaiter<TokenQueue<TCounter>>
TokenQueue<TCounter>::operator co_await()
{
    return Take();
}


template <class TQueue>
bool
TokenQueueAwaiter<TQueue>::await_suspend(tasks::CoroutineHandle handle)
{
    auto wTask = TaskRef(handle).GetWeakPtr();

    CriticalSection cs;

    auto queue = this->source;
    if (queue->numTokens == 0) [[likely]] {
        this->waiter = etl::move(wTask);
        queue->waiters.AddLast(this);
        return true;
    }
    this->SetResult(queue->value - queue->numTokens);
    queue->numTokens--;
    return false;
}

} // namespace pulse

#endif /* TOKEN_QUEUE_H */
