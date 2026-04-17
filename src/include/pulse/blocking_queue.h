#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H

#include <pulse/task.h>
#include <etl/optional.h>


namespace pulse {

template <typename T>
class BlockingQueuePushAwaiter;

template <typename T>
class BlockingQueuePopAwaiter;


/** Implements blocking queue. Objects in the storage are constructed only when in queue.
 *
 * @tparam T Type of value to store. Should be move constructible.
 */
template <typename T>
class BlockingQueue {
public:
    /** Use provided buffer as storage. The buffer lifetime should not be less than this object
     * lifetime. Buffer values should not be initialized (not constructed).
     */
    BlockingQueue(T *buffer, size_t capacity):
        buffer(buffer),
        capacity(capacity)
    {
        PULSE_ASSERT(capacity > 0);
    }

    ~BlockingQueue();

    template <typename U>
    BlockingQueuePushAwaiter<T>
    Push(U &&value);

    template <typename... Args>
    BlockingQueuePushAwaiter<T>
    Emplace(Args &&... args);

    template <typename U>
    bool
    TryPush(U &&value);

    template <typename... Args>
    bool
    TryEmplace(Args &&... args);

    BlockingQueuePopAwaiter<T>
    Pop();

    etl::optional<T>
    TryPop();

private:
    friend class BlockingQueuePushAwaiter<T>;
    friend class BlockingQueuePopAwaiter<T>;

    T * const buffer;
    TailedList<BlockingQueuePushAwaiter<T> *> pushWaiters;
    TailedList<BlockingQueuePopAwaiter<T> *> popWaiters;
    const size_t capacity;
    size_t readIdx = 0, size = 0;

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
        size_t idx = readIdx + size;
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
template <typename T, size_t Capacity>
class InlineBlockingQueue: public BlockingQueue<T> {
public:
    static constexpr size_t capacity = Capacity;

    InlineBlockingQueue():
        BlockingQueue<T>(reinterpret_cast<T *>(buffer), Capacity)
    {}

private:
    alignas(T) uint8_t buffer[sizeof(T) * Capacity];
};


namespace details {

template <typename T>
class BlockingQueueAwaiter {
protected:
    friend class BlockingQueue<T>;

    BlockingQueue<T> *queue = nullptr;
    Task::WeakPtr task;
    alignas(T) uint8_t storage[sizeof(T)];

    BlockingQueueAwaiter() = default;

    BlockingQueueAwaiter(BlockingQueue<T> *queue):
        queue(queue)
    {}

    T &
    Item()
    {
        return *reinterpret_cast<T *>(storage);
    }
};

} // namespace details


template <typename T>
class BlockingQueuePushAwaiter: public details::BlockingQueueAwaiter<T>, public Awaiter<void> {
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
    friend class BlockingQueue<T>;
    friend struct details::ListDefaultTrait<BlockingQueuePushAwaiter<T> *>;

    BlockingQueuePushAwaiter<T> *next = nullptr;


    BlockingQueuePushAwaiter() = default;

    template <typename... Args>
    BlockingQueuePushAwaiter(BlockingQueue<T> *queue, Args &&... args):
        details::BlockingQueueAwaiter<T>(queue)
    {
        etl::construct_at(&this->Item(), etl::forward<Args>(args)...);
    }
};


template <typename T>
class BlockingQueuePopAwaiter: public details::BlockingQueueAwaiter<T>, public Awaiter<T> {
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
        return etl::move(this->Item());
    }
private:
    friend class BlockingQueue<T>;
    friend struct details::ListDefaultTrait<BlockingQueuePopAwaiter<T> *>;

    BlockingQueuePopAwaiter<T> *next = nullptr;

    BlockingQueuePopAwaiter() = default;

    using details::BlockingQueueAwaiter<T>::BlockingQueueAwaiter;

    BlockingQueuePopAwaiter(T &&item)
    {
        etl::construct_at(&this->Item(), etl::move(item));
    }
};


template <typename T>
BlockingQueue<T>::~BlockingQueue()
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

template <typename T>
template <typename U>
BlockingQueuePushAwaiter<T>
BlockingQueue<T>::Push(U &&value)
{
    if (size < capacity) {
        etl::construct_at(&CurWriteItem(), etl::forward<U>(value));
        CommitPush(true);
        return {};
    }
    return BlockingQueuePushAwaiter<T>(this, etl::forward<U>(value));
}

template <typename T>
template <typename... Args>
BlockingQueuePushAwaiter<T>
BlockingQueue<T>::Emplace(Args &&... args)
{
    if (size < capacity) {
        etl::construct_at(&CurWriteItem(), etl::forward<Args>(args)...);
        CommitPush(true);
        return {};
    }
    return BlockingQueuePushAwaiter<T>(this, etl::forward<Args>(args)...);
}

template <typename T>
template <typename U>
bool
BlockingQueue<T>::TryPush(U &&value)
{
    if (size >= capacity) {
        return false;
    }
    etl::construct_at(&CurWriteItem(), etl::forward<U>(value));
    CommitPush(true);
    return true;
}

template <typename T>
template <typename... Args>
bool
BlockingQueue<T>::TryEmplace(Args &&... args)
{
    if (size >= capacity) {
        return false;
    }
    etl::construct_at(&CurWriteItem(), etl::forward<Args>(args)...);
    CommitPush(true);
    return true;
}

template <typename T>
BlockingQueuePopAwaiter<T>
BlockingQueue<T>::Pop()
{
    if (size) {
        T item = etl::move(CurReadItem());
        CommitPop(true);
        return BlockingQueuePopAwaiter<T>(etl::move(item));
    }
    return BlockingQueuePopAwaiter<T>(this);
}

template <typename T>
etl::optional<T>
BlockingQueue<T>::TryPop()
{
    if (size) {
        T item = etl::move(CurReadItem());
        CommitPop(true);
        return etl::optional<T>(etl::move(item));
    }
    return etl::nullopt;
}

template <typename T>
void
BlockingQueue<T>::CommitPush(bool checkWaiters)
{
    PULSE_ASSERT(size <= capacity);
    size++;
    if (checkWaiters) {
        return;
    }
    while (size && !popWaiters.IsEmpty()) {
        auto waiter = popWaiters.PopFirst();
        CommitPop(false);
        etl::construct_at(&waiter->Item(), etl::move(CurReadItem()));
        waiter->queue = nullptr;
        waiter->task.Wakeup();
    }
}

template <typename T>
void
BlockingQueue<T>::CommitPop(bool checkWaiters)
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
        waiter->task.Wakeup();
    }
}


template <typename T>
BlockingQueuePushAwaiter<T>::~BlockingQueuePushAwaiter()
{
    if (this->queue) {
        etl::destroy_at(&this->Item());
        this->queue->pushWaiters.Remove(this);
    }
}

template <typename T>
bool
BlockingQueuePushAwaiter<T>::await_suspend(Task::CoroutineHandle handle)
{
    if (!this->queue) {
        return false;
    }
    if (this->queue->size < this->queue->capacity) {
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


template <typename T>
BlockingQueuePopAwaiter<T>::~BlockingQueuePopAwaiter()
{
    if (this->queue) {
        this->queue->popWaiters.Remove(this);
    } else {
        etl::destroy_at(&this->Item());
    }
}

template <typename T>
bool
BlockingQueuePopAwaiter<T>::await_suspend(Task::CoroutineHandle handle)
{
    if (!this->queue) {
        return false;
    }
    if (this->queue->size) {
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
