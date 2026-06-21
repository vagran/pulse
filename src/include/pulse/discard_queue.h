#ifndef DISCARD_QUEUE_H
#define DISCARD_QUEUE_H

#include <pulse/task.h>
#include <pulse/port.h>
#include <etl/optional.h>


namespace pulse {

template <typename TQueue>
class DiscardQueuePopAwaiter;


/** Implements queue with discard on overflow (configurable tail or head discard policy). Can block
 * consumer. Producer can be in ISR. Objects in the storage are constructed only when in queue.
 *
 * @tparam T Type of value to store. Should be move constructible.
 * @tparam tailDrop Discard newly pushed items on overflow when true, or least recently pushed
 *  items when false.
 * @tparam TIndex Type used to stored sizes and indices. Capacity should fit into this type.
 */
template <typename T, bool tailDrop, etl::unsigned_integral TIndex = size_t>
class DiscardQueue {
public:
    /** Use provided buffer as storage. The buffer lifetime should not be less than this object
     * lifetime. Buffer values should not be initialized (not constructed).
     */
    DiscardQueue(T *buffer, TIndex capacity):
        buffer(buffer),
        capacity(capacity)
    {
        PULSE_ASSERT(capacity > 0);
        PULSE_ASSERT(capacity <= etl::numeric_limits<TIndex>::max());
    }

    ~DiscardQueue();

    /**
     * @return True if no discard occurred, false if some item discarded.
     */
    template <typename U>
    bool
    Push(U &&value);

    template <typename... Args>
    bool
    Emplace(Args &&... args);

    DiscardQueuePopAwaiter<DiscardQueue>
    Pop();

    DiscardQueuePopAwaiter<DiscardQueue>
    operator co_await();

    etl::optional<T>
    TryPop();

    /// Get number of queued elements. Should be used only if producer is not in ISR or with
    // interrupts disabled.
    TIndex
    Size() const
    {
        return size;
    }

    /// Check if queue is empty. Should be used only if producer is not in ISR or with
    // interrupts disabled.
    bool
    IsEmpty() const
    {
        return size == 0;
    }


    /** Get reference to the first item (which is next to be popped out). Valid only if not empty.
     * Should be used only if producer is not in ISR or with interrupts disabled. Also taildrop
     * queue is safe for unprotected access.
     */
    T &
    Peek()
    {
        return CurReadItem();
    }

    /** Get reference to the last pushed item. Valid only if not empty. Should be used only if
     * producer is not in ISR or with interrupts disabled.
     */
    T &
    PeekLast()
    {
        PULSE_ASSERT(size);
        TIndex idx = readIdx + size - 1;
        if (idx >= capacity) {
            idx -= capacity;
        }
        return buffer[idx];
    }

private:
    struct AwaiterSourceTrait;
    using TAwaiterBase = details::AwaiterBase<T, DiscardQueue, AwaiterSourceTrait>;

    friend class DiscardQueuePopAwaiter<DiscardQueue>;

    T * const buffer;
    TailedList<TAwaiterBase *> popWaiters;
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
    CommitPush();

    void
    CommitPop();

    struct AwaiterSourceTrait {
        static void
        DequeueAwaiter(DiscardQueue *queue, TAwaiterBase *awaiter)
        {
            queue->popWaiters.Remove(awaiter);
        }
    };
};


/** DiscardQueue with embedded fixed size storage. */
template <typename T, bool tailDrop, size_t Capacity,
          etl::unsigned_integral TIndex = pulse::SizedUint<pulse::UintBitWidth(Capacity)>>
class InlineDiscardQueue: public DiscardQueue<T, tailDrop, TIndex> {
public:
    static constexpr size_t capacity = Capacity;

    InlineDiscardQueue():
        DiscardQueue<T, tailDrop, TIndex>(reinterpret_cast<T *>(buffer), Capacity)
    {
        static_assert(Capacity <= etl::numeric_limits<TIndex>::max());
    }

private:
    alignas(T) uint8_t buffer[sizeof(T) * Capacity];
};


template <class TQueue>
class DiscardQueuePopAwaiter: public TQueue::TAwaiterBase {
public:
    bool
    await_suspend(tasks::CoroutineHandle handle);

private:
    friend TQueue;
    using Base = TQueue::TAwaiterBase;

    using Base::Base;
};


template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
DiscardQueue<T, tailDrop, TIndex>::~DiscardQueue()
{
    CriticalSection cs;
    for (auto waiter: popWaiters) {
        if (!waiter->Wakeup()) {
            continue;
        }
        // Return default-constructed item.
        waiter->SetResult();
    }
    cs.Exit();

    while (size) {
        etl::destroy_at(&CurReadItem());
        readIdx++;
        if (readIdx >= capacity) {
            readIdx = 0;
        }
        size--;
    }
}

template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
template <typename U>
bool
DiscardQueue<T, tailDrop, TIndex>::Push(U &&value)
{
    CriticalSection cs;

    if (size) {
        PULSE_ASSERT(popWaiters.IsEmpty());
    }

    if (size < capacity) {
        etl::construct_at(&CurWriteItem(), etl::forward<U>(value));
        CommitPush();
        return true;
    }

    if constexpr (!tailDrop) {
        // Discard least recently added item, and push a new one
        T &item = CurReadItem();
        etl::destroy_at(&item);
        etl::construct_at(&item, etl::forward<U>(value));
        readIdx++;
        if (readIdx >= capacity) {
            readIdx = 0;
        }
    }

    return false;
}

template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
template <typename... Args>
bool
DiscardQueue<T, tailDrop, TIndex>::Emplace(Args &&... args)
{
    CriticalSection cs;

    if (size) {
        PULSE_ASSERT(popWaiters.IsEmpty());
    }

    if (size < capacity) {
        etl::construct_at(&CurWriteItem(), etl::forward<Args>(args)...);
        CommitPush();
        return true;
    }

    if constexpr (!tailDrop) {
        // Discard least recently added item, and push a new one
        T &item = CurReadItem();
        etl::destroy_at(&item);
        etl::construct_at(&item, etl::forward<Args>(args)...);
        readIdx++;
        if (readIdx >= capacity) {
            readIdx = 0;
        }
    }

    return false;
}

template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
DiscardQueuePopAwaiter<DiscardQueue<T, tailDrop, TIndex>>
DiscardQueue<T, tailDrop, TIndex>::Pop()
{
    CriticalSection cs;

    if (size) {
        PULSE_ASSERT(popWaiters.IsEmpty());
        // Awaiters do not have copy constructors so temporarily store item here.
        T item = etl::move(CurReadItem());
        CommitPop();
        return DiscardQueuePopAwaiter<DiscardQueue<T, tailDrop, TIndex>>(etl::move(item));
    }
    return DiscardQueuePopAwaiter<DiscardQueue<T, tailDrop, TIndex>>(this);
}

template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
DiscardQueuePopAwaiter<DiscardQueue<T, tailDrop, TIndex>>
DiscardQueue<T, tailDrop, TIndex>::operator co_await()
{
    return Pop();
}

template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
etl::optional<T>
DiscardQueue<T, tailDrop, TIndex>::TryPop()
{
    CriticalSection cs;

    if (size) {
        T item = etl::move(CurReadItem());
        CommitPop();
        return etl::optional<T>(etl::move(item));
    }
    return etl::nullopt;
}

template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
void
DiscardQueue<T, tailDrop, TIndex>::CommitPush()
{
    PULSE_ASSERT(size <= capacity);
    size++;
    while (size && !popWaiters.IsEmpty()) {
        auto waiter = popWaiters.PopFirst();
        if (!waiter->Wakeup()) {
            // Actually this unlikely to happen since Awaiter usually sits in waiter coroutine
            // frame, so when task last reference is released, the frame is destroyed and awaiter
            // destructor removes itself from the list. But make it so for consistency and
            // robustness for some exotic cases when awaiter is outside waiter task frame.
            continue;
        }
        waiter->SetResult(etl::move(CurReadItem()));
        CommitPop();
    }
}

template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
void
DiscardQueue<T, tailDrop, TIndex>::CommitPop()
{
    PULSE_ASSERT(size != 0);
    etl::destroy_at(&CurReadItem());
    size--;
    readIdx++;
    if (readIdx >= capacity) {
        readIdx = 0;
    }
}


template <class TQueue>
bool
DiscardQueuePopAwaiter<TQueue>::await_suspend(tasks::CoroutineHandle handle)
{
    CriticalSection cs;

    auto queue = this->source;
    if (queue->size) [[unlikely]] {
        PULSE_ASSERT(queue->popWaiters.IsEmpty());
        this->SetResult(etl::move(queue->CurReadItem()));
        queue->CommitPop();
        return false;
    }
    this->waiter = TaskRef(handle).GetWeakPtr();
    this->source->popWaiters.AddLast(this);
    return true;
}

} // namespace pulse

#endif /* DISCARD_QUEUE_H */
