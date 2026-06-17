#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <pulse/details/common.h>
#include <etl/memory.h>


namespace pulse {

/// Generic ring buffer. Suitable for non-trivial objects. Supports moveable-only types.
template <class T, typename TIndex = size_t>
class RingBuffer {
public:
    const TIndex capacity;

    RingBuffer(T *buffer, TIndex capacity):
        capacity(capacity),
        buffer(buffer)
    {
        PULSE_ASSERT(capacity > 0);
        // GetWritePos() computes readPos + size, which can reach 2 * capacity - 2.
        PULSE_ASSERT(capacity <= etl::numeric_limits<TIndex>::max() / 2 + 1);
    }

    RingBuffer(const RingBuffer &) = delete;
    RingBuffer(RingBuffer &&) = delete;
    RingBuffer &operator=(const RingBuffer &) = delete;
    RingBuffer &operator=(RingBuffer &&) = delete;

    ~RingBuffer()
    {
        Clear();
    }

    /**
     * @return TIndex Number of elements currently contained in the buffer.
     */
    TIndex
    GetSize() const
    {
        return size;
    }

    /**
     * @return TIndex Number of elements still can write into the buffer.
     */
    TIndex
    GetAvailableCapacity() const
    {
        return capacity - GetSize();
    }

    bool
    IsEmpty() const
    {
        return GetSize() == 0;
    }

    bool
    IsFull() const
    {
        return GetAvailableCapacity() == 0;
    }

    /// Push item to the buffer.
    /// @return Pointer to added item, null if no space.
    template <typename... TArgs>
    T *
    Push(TArgs &&... args)
    {
        if (IsFull()) {
            return nullptr;
        }
        T *p = &GetWriteItem();
        etl::construct_at(p, etl::forward<TArgs>(args)...);
        size++;
        return p;
    }

    T
    Pop()
    {
        T *p = &GetReadItem();
        T item = etl::move(*p);
        CommitPop();
        return item;
    }

    T &
    Peek()
    {
        return GetReadItem();
    }

    const T &
    Peek() const
    {
        return GetReadItem();
    }

    void
    RemoveFirst()
    {
        CommitPop();
    }

    /// Destroy all elements currently contained in the buffer.
    void
    Clear();

private:
    T * const buffer;
    TIndex readPos = 0, size = 0;

    TIndex
    GetWritePos() const
    {
        TIndex pos = readPos + size;
        if (pos >= capacity) {
            return pos - capacity;
        }
        return pos;
    }

    T &
    GetWriteItem() const
    {
        PULSE_ASSERT(!IsFull());
        return buffer[GetWritePos()];
    }

    T &
    GetReadItem() const
    {
        PULSE_ASSERT(!IsEmpty());
        return buffer[readPos];
    }

    void
    CommitPop();
};


/** RingBuffer with embedded fixed size storage. */
template <typename T, size_t Capacity,
          etl::unsigned_integral TIndex = pulse::SizedUint<pulse::UintBitWidth(Capacity * 2 - 1)>>
class InlineRingBuffer: public RingBuffer<T, TIndex> {
public:
    InlineRingBuffer():
        RingBuffer<T, TIndex>(buffer, Capacity)
    {
        static_assert(Capacity * 2 - 1 <= etl::numeric_limits<TIndex>::max());
    }

    // User-provided destructor is required: the anonymous union below makes this a union-like
    // class, so a defaulted destructor would be deleted for non-trivial T. The body is empty
    // because the base RingBuffer destructor destroys any remaining elements; the union prevents
    // implicit destruction of the (otherwise managed) storage.
    ~InlineRingBuffer()
    {}

private:
    // Prevent default construction.
    union {
        T buffer[Capacity];
    };
};


template <class T, typename TIndex>
void
RingBuffer<T, TIndex>::CommitPop()
{
    PULSE_ASSERT(!IsEmpty());
    etl::destroy_at(&GetReadItem());
    size--;
    readPos++;
    if (readPos >= capacity) {
        readPos = 0;
    }
}

template <class T, typename TIndex>
void
RingBuffer<T, TIndex>::Clear()
{
    while (!IsEmpty()) {
        CommitPop();
    }
}

} // namespace pulse

#endif /* RING_BUFFER_H */
