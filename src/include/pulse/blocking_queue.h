#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H

#include <pulse/task.h>
#include <etl/optional.h>


namespace pulse {

template <class TQueue>
class BlockingQueuePushAwaiter;

template <class TQueue>
class BlockingQueuePopAwaiter;


/** Implements blocking queue. Can block both producer and consumer. Can be used for communication
 * between applications tasks only, cannot be used from ISR (use `DiscardQueue` to allow ISR
 * producer). Objects in the storage are constructed only when in queue.
 *
 * @tparam T Type of value to store. Should be move constructible.
 * @tparam TIndex Type used to stored sizes and indices. Capacity should fit into this type.
 */
template <typename T, etl::unsigned_integral TIndex = size_t>
class BlockingQueue {
public:
    /** Use provided buffer as storage. The buffer lifetime should not be less than this object
     * lifetime. Buffer values should not be initialized (not constructed).
     */
    BlockingQueue(T *buffer, TIndex capacity):
        buffer(buffer),
        capacity(capacity)
    {
        PULSE_ASSERT(capacity > 0);
        PULSE_ASSERT(capacity <= etl::numeric_limits<TIndex>::max());
    }

    ~BlockingQueue();

    template <typename U>
    BlockingQueuePushAwaiter<BlockingQueue>
    Push(U &&value);

    template <typename... Args>
    BlockingQueuePushAwaiter<BlockingQueue>
    Emplace(Args &&... args);

    template <typename U>
    bool
    TryPush(U &&value);

    template <typename... Args>
    bool
    TryEmplace(Args &&... args);

    BlockingQueuePopAwaiter<BlockingQueue>
    Pop();

    etl::optional<T>
    TryPop();

private:
    struct PushAwaiterSourceTrait;
    struct PopAwaiterSourceTrait;
    using TAbstractPushAwaiter = details::AbstractAwaiter<T, BlockingQueue, PushAwaiterSourceTrait>;
    using TAbstractPopAwaiter = details::AbstractAwaiter<T, BlockingQueue, PopAwaiterSourceTrait>;

    friend class BlockingQueuePushAwaiter<BlockingQueue>;
    friend class BlockingQueuePopAwaiter<BlockingQueue>;

    T * const buffer;
    TailedList<TAbstractPushAwaiter *> pushWaiters;
    TailedList<TAbstractPopAwaiter *> popWaiters;
    const TIndex capacity;
    TIndex readIdx = 0, size = 0;

    T &
    CurReadItem()
    {
        PULSE_ASSERT(size);
        return buffer[readIdx];
    }

    T &
    CurWriteItem()
    {
        PULSE_ASSERT(size < capacity);
        TIndex idx = readIdx + size;
        if (idx >= capacity) {
            idx -= capacity;
        }
        return buffer[idx];
    }

    void
    CommitPush(bool checkWaiters);

    void
    CommitPop(bool checkWaiters);

    struct PushAwaiterSourceTrait {
        static void
        DequeueAwaiter(BlockingQueue *queue, TAbstractPushAwaiter *awaiter)
        {
            queue->pushWaiters.Remove(awaiter);
        }
    };

    struct PopAwaiterSourceTrait {
        static void
        DequeueAwaiter(BlockingQueue *queue, TAbstractPopAwaiter *awaiter)
        {
            queue->popWaiters.Remove(awaiter);
        }
    };
};


/** BlockingQueue with embedded fixed size storage. */
template <typename T, size_t Capacity,
          etl::unsigned_integral TIndex = pulse::SizedUint<pulse::UintBitWidth(Capacity)>>
class InlineBlockingQueue: public BlockingQueue<T, TIndex> {
public:
    static constexpr size_t capacity = Capacity;

    InlineBlockingQueue():
        BlockingQueue<T, TIndex>(reinterpret_cast<T *>(buffer), Capacity)
    {
        static_assert(Capacity <= etl::numeric_limits<TIndex>::max());
    }

private:
    alignas(T) uint8_t buffer[sizeof(T) * Capacity];
};


/** Awaiter produced by `BlockingQueue::Push()`/`Emplace()`. It carries the item to be pushed in the
 * base awaiter storage while suspended. The item is alive while `source` is set (still pending); on
 * completion the item is moved into the queue and the moved-from value is destroyed by the
 * `AbstractAwaiter` base destructor.
 */
template <class TQueue>
class BlockingQueuePushAwaiter: public TQueue::TAbstractPushAwaiter {
public:
    ~BlockingQueuePushAwaiter();

    bool
    await_suspend(tasks::CoroutineHandle handle);

    void
    await_resume() const
    {}

private:
    friend TQueue;
    using Base = TQueue::TAbstractPushAwaiter;

    template <typename... Args>
    BlockingQueuePushAwaiter(TQueue *queue, Args &&... args):
        Base(queue)
    {
        etl::construct_at(&this->Result(), etl::forward<Args>(args)...);
        // Push eagerly when there is free space, so that a non-awaited `Push()`/`Emplace()` still
        // enqueues the item. Otherwise stay pending (the item lives in the awaiter storage until
        // co_awaited or discarded).
        if (queue->size < queue->capacity) {
            etl::construct_at(&queue->CurWriteItem(), etl::move(this->Result()));
            queue->CommitPush(true);
            // Mark completed; the moved-from item is destroyed by the base destructor.
            this->source = nullptr;
        }
    }
};


template <class TQueue>
class BlockingQueuePopAwaiter: public TQueue::TAbstractPopAwaiter {
public:
    bool
    await_suspend(tasks::CoroutineHandle handle);

private:
    friend TQueue;
    using Base = TQueue::TAbstractPopAwaiter;

    using Base::Base;
};


template <typename T, etl::unsigned_integral TIndex>
BlockingQueue<T, TIndex>::~BlockingQueue()
{
    for (auto waiter: pushWaiters) {
        if (!waiter->Wakeup()) {
            continue;
        }
        // The pending item was never pushed; leave it intact, it is destroyed by the awaiter
        // base destructor when the producer task frame is released.
        waiter->source = nullptr;
    }
    for (auto waiter: popWaiters) {
        if (!waiter->Wakeup()) {
            continue;
        }
        // Return default-constructed item.
        waiter->SetResult();
    }

    while (size) {
        etl::destroy_at(&CurReadItem());
        readIdx++;
        if (readIdx >= capacity) {
            readIdx = 0;
        }
        size--;
    }
}

template <typename T, etl::unsigned_integral TIndex>
template <typename U>
BlockingQueuePushAwaiter<BlockingQueue<T, TIndex>>
BlockingQueue<T, TIndex>::Push(U &&value)
{
    return BlockingQueuePushAwaiter<BlockingQueue<T, TIndex>>(this, etl::forward<U>(value));
}

template <typename T, etl::unsigned_integral TIndex>
template <typename... Args>
BlockingQueuePushAwaiter<BlockingQueue<T, TIndex>>
BlockingQueue<T, TIndex>::Emplace(Args &&... args)
{
    return BlockingQueuePushAwaiter<BlockingQueue<T, TIndex>>(this, etl::forward<Args>(args)...);
}

template <typename T, etl::unsigned_integral TIndex>
template <typename U>
bool
BlockingQueue<T, TIndex>::TryPush(U &&value)
{
    if (size >= capacity) {
        return false;
    }
    etl::construct_at(&CurWriteItem(), etl::forward<U>(value));
    CommitPush(true);
    return true;
}

template <typename T, etl::unsigned_integral TIndex>
template <typename... Args>
bool
BlockingQueue<T, TIndex>::TryEmplace(Args &&... args)
{
    if (size >= capacity) {
        return false;
    }
    etl::construct_at(&CurWriteItem(), etl::forward<Args>(args)...);
    CommitPush(true);
    return true;
}

template <typename T, etl::unsigned_integral TIndex>
BlockingQueuePopAwaiter<BlockingQueue<T, TIndex>>
BlockingQueue<T, TIndex>::Pop()
{
    if (size) {
        // Awaiters do not have copy constructors so temporarily store item here.
        T item = etl::move(CurReadItem());
        CommitPop(true);
        return BlockingQueuePopAwaiter<BlockingQueue<T, TIndex>>(etl::move(item));
    }
    return BlockingQueuePopAwaiter<BlockingQueue<T, TIndex>>(this);
}

template <typename T, etl::unsigned_integral TIndex>
etl::optional<T>
BlockingQueue<T, TIndex>::TryPop()
{
    if (size) {
        T item = etl::move(CurReadItem());
        CommitPop(true);
        return etl::optional<T>(etl::move(item));
    }
    return etl::nullopt;
}

template <typename T, etl::unsigned_integral TIndex>
void
BlockingQueue<T, TIndex>::CommitPush(bool checkWaiters)
{
    PULSE_ASSERT(size <= capacity);
    size++;
    if (!checkWaiters) {
        return;
    }
    while (size && !popWaiters.IsEmpty()) {
        auto waiter = popWaiters.PopFirst();
        if (!waiter->Wakeup()) {
            continue;
        }
        waiter->SetResult(etl::move(CurReadItem()));
        CommitPop(false);
    }
}

template <typename T, etl::unsigned_integral TIndex>
void
BlockingQueue<T, TIndex>::CommitPop(bool checkWaiters)
{
    PULSE_ASSERT(size != 0);
    etl::destroy_at(&CurReadItem());
    size--;
    readIdx++;
    if (readIdx >= capacity) {
        readIdx = 0;
    }
    if (!checkWaiters) {
        return;
    }
    while (size < capacity && !pushWaiters.IsEmpty()) {
        auto waiter = pushWaiters.PopFirst();
        if (!waiter->Wakeup()) {
            continue;
        }
        etl::construct_at(&CurWriteItem(), etl::move(waiter->Result()));
        CommitPush(false);
        // Leave the moved-from item in the awaiter storage; it is destroyed by the awaiter base
        // destructor once the producer task resumes and its frame is released.
        waiter->source = nullptr;
    }
}


template <class TQueue>
BlockingQueuePushAwaiter<TQueue>::~BlockingQueuePushAwaiter()
{
    if (this->source) {
        // Still pending: the item was never pushed, destroy it here. List removal (if queued) is
        // handled by the base destructor.
        etl::destroy_at(&this->Result());
    }
}

template <class TQueue>
bool
BlockingQueuePushAwaiter<TQueue>::await_suspend(tasks::CoroutineHandle handle)
{
    auto queue = this->source;
    if (queue->size < queue->capacity) {
        etl::construct_at(&queue->CurWriteItem(), etl::move(this->Result()));
        queue->CommitPush(true);
        // Mark completed; the moved-from item is destroyed by the base destructor.
        this->source = nullptr;
        return false;
    }
    this->waiter = TaskRef(handle).GetWeakPtr();
    queue->pushWaiters.AddLast(this);
    return true;
}


template <class TQueue>
bool
BlockingQueuePopAwaiter<TQueue>::await_suspend(tasks::CoroutineHandle handle)
{
    auto queue = this->source;
    if (queue->size) [[unlikely]] {
        this->SetResult(etl::move(queue->CurReadItem()));
        queue->CommitPop(true);
        return false;
    }
    this->waiter = TaskRef(handle).GetWeakPtr();
    queue->popWaiters.AddLast(this);
    return true;
}

} // namespace pulse

#endif /* BLOCKING_QUEUE_H */
