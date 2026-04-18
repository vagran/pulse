#ifndef DISCARD_QUEUE_H
#define DISCARD_QUEUE_H

#include <pulse/task.h>
#include <pulse/port.h>
#include <etl/optional.h>


namespace pulse {

template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
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

    DiscardQueuePopAwaiter<T, tailDrop, TIndex>
    Pop();

    etl::optional<T>
    TryPop();

private:
    friend class DiscardQueuePopAwaiter<T, tailDrop, TIndex>;

    T * const buffer;
    TailedList<DiscardQueuePopAwaiter<T, tailDrop, TIndex> *> popWaiters;
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
    CommitPop();
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


template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
class DiscardQueuePopAwaiter: public Awaiter<T> {
public:
    ~DiscardQueuePopAwaiter();

    bool
    await_ready() const
    {
        return !this->queue;
    }

    bool
    await_suspend(Task::CoroutineHandle handle);

    T
    await_resume()
    {
        PULSE_ASSERT(!this->queue);
        return etl::move(this->Item());
    }
private:
    friend class DiscardQueue<T, tailDrop, TIndex>;
    friend struct details::ListDefaultTrait<DiscardQueuePopAwaiter<T, tailDrop, TIndex> *>;

    DiscardQueuePopAwaiter<T, tailDrop, TIndex> *next = nullptr;

    DiscardQueue<T, tailDrop, TIndex> *queue = nullptr;
    Task::WeakPtr task;
    alignas(T) uint8_t storage[sizeof(T)];

    DiscardQueuePopAwaiter() = default;

    DiscardQueuePopAwaiter(DiscardQueue<T, tailDrop, TIndex> *queue):
        queue(queue)
    {}

    DiscardQueuePopAwaiter(T &&item)
    {
        etl::construct_at(&this->Item(), etl::move(item));
    }

    T &
    Item()
    {
        return *reinterpret_cast<T *>(storage);
    }
};


template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
DiscardQueue<T, tailDrop, TIndex>::~DiscardQueue()
{
    for (auto waiter: popWaiters) {
        waiter->queue = nullptr;
        // Return default-constructed item.
        etl::construct_at(&waiter->Item());
        waiter->task.Wakeup();
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

template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
template <typename U>
bool
DiscardQueue<T, tailDrop, TIndex>::Push(U &&value)
{
    CriticalSection cs;

    if (size < capacity) {
        etl::construct_at(&CurWriteItem(), etl::forward<U>(value));
        CommitPush(true);
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

    if (size < capacity) {
        etl::construct_at(&CurWriteItem(), etl::forward<Args>(args)...);
        CommitPush(true);
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
DiscardQueuePopAwaiter<T, tailDrop, TIndex>
DiscardQueue<T, tailDrop, TIndex>::Pop()
{
    CriticalSection cs;

    if (size) {
        // Awaiters do not have copy constructors so temporarily store item here.
        T item = etl::move(CurReadItem());
        CommitPop();
        return DiscardQueuePopAwaiter<T, tailDrop, TIndex>(etl::move(item));
    }
    return DiscardQueuePopAwaiter<T, tailDrop, TIndex>(this);
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
DiscardQueue<T, tailDrop, TIndex>::CommitPush(bool checkWaiters)
{
    PULSE_ASSERT(size <= capacity);
    size++;
    if (!checkWaiters) {
        return;
    }
    while (size && !popWaiters.IsEmpty()) {
        auto waiter = popWaiters.PopFirst();
        etl::construct_at(&waiter->Item(), etl::move(CurReadItem()));
        CommitPop();
        waiter->queue = nullptr;
        waiter->task.Wakeup();
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


template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
DiscardQueuePopAwaiter<T, tailDrop, TIndex>::~DiscardQueuePopAwaiter()
{
    if (queue) {
        if (task) {
            queue->popWaiters.Remove(this);
        }
    } else {
        etl::destroy_at(&this->Item());
    }
}

template <typename T, bool tailDrop, etl::unsigned_integral TIndex>
bool
DiscardQueuePopAwaiter<T, tailDrop, TIndex>::await_suspend(Task::CoroutineHandle handle)
{
    if (!queue) {
        return false;
    }
    CriticalSection cs;

    if (queue->size) {
        etl::construct_at(&Item(), etl::move(queue->CurReadItem()));
        queue->CommitPop();
        queue = nullptr;
        return false;
    }
    task = Task(handle).GetWeakPtr();
    queue->popWaiters.AddLast(this);
    return true;
}

} // namespace pulse

#endif /* DISCARD_QUEUE_H */
