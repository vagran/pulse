#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H

#include <pulse/task.h>
#include <etl/optional.h>


namespace pulse {

template <typename T, etl::unsigned_integral TIndex>
class BlockingQueuePushAwaiter;

template <typename T, etl::unsigned_integral TIndex>
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
    BlockingQueuePushAwaiter<T, TIndex>
    Push(U &&value);

    template <typename... Args>
    BlockingQueuePushAwaiter<T, TIndex>
    Emplace(Args &&... args);

    template <typename U>
    bool
    TryPush(U &&value);

    template <typename... Args>
    bool
    TryEmplace(Args &&... args);

    BlockingQueuePopAwaiter<T, TIndex>
    Pop();

    etl::optional<T>
    TryPop();

private:
    friend class BlockingQueuePushAwaiter<T, TIndex>;
    friend class BlockingQueuePopAwaiter<T, TIndex>;

    T * const buffer;
    TailedList<BlockingQueuePushAwaiter<T, TIndex> *> pushWaiters;
    TailedList<BlockingQueuePopAwaiter<T, TIndex> *> popWaiters;
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


namespace details {

template <typename T, etl::unsigned_integral TIndex>
class BlockingQueueAwaiter {
protected:
    friend class BlockingQueue<T, TIndex>;

    BlockingQueue<T, TIndex> *queue = nullptr;
    Task::WeakPtr task;
    alignas(T) uint8_t storage[sizeof(T)];

    BlockingQueueAwaiter() = default;

    BlockingQueueAwaiter(BlockingQueue<T, TIndex> *queue):
        queue(queue)
    {}

    T &
    Item()
    {
        return *reinterpret_cast<T *>(storage);
    }
};

} // namespace details


template <typename T, etl::unsigned_integral TIndex>
class BlockingQueuePushAwaiter: public details::BlockingQueueAwaiter<T, TIndex>,
    public Awaiter<void> {
public:
    ~BlockingQueuePushAwaiter();

    bool
    await_ready() const
    {
        return !this->queue;
    }

    bool
    await_suspend(Task::CoroutineHandle handle);

    void
    await_resume() const
    {}

private:
    friend class BlockingQueue<T, TIndex>;
    friend struct details::ListDefaultTrait<BlockingQueuePushAwaiter<T, TIndex> *>;

    BlockingQueuePushAwaiter<T, TIndex> *next = nullptr;


    BlockingQueuePushAwaiter() = default;

    template <typename... Args>
    BlockingQueuePushAwaiter(BlockingQueue<T, TIndex> *queue, Args &&... args):
        details::BlockingQueueAwaiter<T, TIndex>(queue)
    {
        etl::construct_at(&this->Item(), etl::forward<Args>(args)...);
    }
};


template <typename T, etl::unsigned_integral TIndex>
class BlockingQueuePopAwaiter: public details::BlockingQueueAwaiter<T, TIndex>, public Awaiter<T> {
public:
    ~BlockingQueuePopAwaiter();

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
    friend class BlockingQueue<T, TIndex>;
    friend struct details::ListDefaultTrait<BlockingQueuePopAwaiter<T, TIndex> *>;

    BlockingQueuePopAwaiter<T, TIndex> *next = nullptr;

    BlockingQueuePopAwaiter() = default;

    using details::BlockingQueueAwaiter<T, TIndex>::BlockingQueueAwaiter;

    BlockingQueuePopAwaiter(T &&item)
    {
        etl::construct_at(&this->Item(), etl::move(item));
    }
};


template <typename T, etl::unsigned_integral TIndex>
BlockingQueue<T, TIndex>::~BlockingQueue()
{
    for (auto waiter: pushWaiters) {
        waiter->queue = nullptr;
        etl::destroy_at(&waiter->Item());
        waiter->task.Wakeup();
    }
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

template <typename T, etl::unsigned_integral TIndex>
template <typename U>
BlockingQueuePushAwaiter<T, TIndex>
BlockingQueue<T, TIndex>::Push(U &&value)
{
    if (size < capacity) {
        etl::construct_at(&CurWriteItem(), etl::forward<U>(value));
        CommitPush(true);
        return {};
    }
    return BlockingQueuePushAwaiter<T, TIndex>(this, etl::forward<U>(value));
}

template <typename T, etl::unsigned_integral TIndex>
template <typename... Args>
BlockingQueuePushAwaiter<T, TIndex>
BlockingQueue<T, TIndex>::Emplace(Args &&... args)
{
    if (size < capacity) {
        etl::construct_at(&CurWriteItem(), etl::forward<Args>(args)...);
        CommitPush(true);
        return {};
    }
    return BlockingQueuePushAwaiter<T, TIndex>(this, etl::forward<Args>(args)...);
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
BlockingQueuePopAwaiter<T, TIndex>
BlockingQueue<T, TIndex>::Pop()
{
    if (size) {
        // Awaiters do not have copy constructors so temporarily store item here.
        T item = etl::move(CurReadItem());
        CommitPop(true);
        return BlockingQueuePopAwaiter<T, TIndex>(etl::move(item));
    }
    return BlockingQueuePopAwaiter<T, TIndex>(this);
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
        etl::construct_at(&waiter->Item(), etl::move(CurReadItem()));
        CommitPop(false);
        waiter->queue = nullptr;
        waiter->task.Wakeup();
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
        etl::construct_at(&CurWriteItem(), etl::move(waiter->Item()));
        CommitPush(false);
        waiter->queue = nullptr;
        etl::destroy_at(&waiter->Item());
        waiter->task.Wakeup();
    }
}


template <typename T, etl::unsigned_integral TIndex>
BlockingQueuePushAwaiter<T, TIndex>::~BlockingQueuePushAwaiter()
{
    if (this->queue) {
        etl::destroy_at(&this->Item());
        if (this->task) {
            this->queue->pushWaiters.Remove(this);
        }
    }
}

template <typename T, etl::unsigned_integral TIndex>
bool
BlockingQueuePushAwaiter<T, TIndex>::await_suspend(Task::CoroutineHandle handle)
{
    if (!this->queue) {
        return false;
    }
    if (this->queue->size < this->queue->capacity) [[unlikely]] {
        etl::construct_at(&this->queue->CurWriteItem(), etl::move(this->Item()));
        this->queue->CommitPush(true);
        this->queue = nullptr;
        etl::destroy_at(&this->Item());
        return false;
    }
    this->task = Task(handle).GetWeakPtr();
    this->queue->pushWaiters.AddLast(this);
    return true;
}


template <typename T, etl::unsigned_integral TIndex>
BlockingQueuePopAwaiter<T, TIndex>::~BlockingQueuePopAwaiter()
{
    if (this->queue) {
        if (this->task) {
            this->queue->popWaiters.Remove(this);
        }
    } else {
        etl::destroy_at(&this->Item());
    }
}

template <typename T, etl::unsigned_integral TIndex>
bool
BlockingQueuePopAwaiter<T, TIndex>::await_suspend(Task::CoroutineHandle handle)
{
    if (!this->queue) {
        return false;
    }
    if (this->queue->size) [[unlikely]] {
        etl::construct_at(&this->Item(), etl::move(this->queue->CurReadItem()));
        this->queue->CommitPop(true);
        this->queue = nullptr;
        return false;
    }
    this->task = Task(handle).GetWeakPtr();
    this->queue->popWaiters.AddLast(this);
    return true;
}

} // namespace pulse

#endif /* BLOCKING_QUEUE_H */
