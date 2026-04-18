#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <pulse/details/common.h>
#include <etl/span.h>


namespace pulse {

/** Light-weight ring buffer implementation. Buffer size should be strictly power of two to enable
 * fast math.
 * @tparam T Type of values to store.
 * @tparam TIndex Type of index field.
 */
template <typename T, etl::unsigned_integral TIndex = size_t>
class RingBuffer {
private:
    T * const buffer;
public:
    const TIndex capacity;

    RingBuffer(T *buffer, TIndex capacity):
        buffer(buffer),
        capacity(capacity)
    {
        PULSE_ASSERT(capacity > 0);
        PULSE_ASSERT(PULSE_IS_POW2(capacity));
        PULSE_ASSERT(capacity * 2 <= etl::numeric_limits<TIndex>::max());
    }

    /**
     * @return TIndex Number of elements currently contained in the buffer.
     */
    TIndex
    GetSize() const
    {
        return writePos - readPos;
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

    /** Write data into the buffer, not more than currently available capacity.
     * @return TIndex Actually added data size.
     */
    TIndex
    Write(const T *data, TIndex size)
    {
        size = etl::min(size, GetAvailableCapacity());
        TIndex bufferWritePos = writePos & (capacity - 1);
        TIndex tailSize = etl::min<TIndex>(size, capacity - bufferWritePos);
        etl::copy(data, data + tailSize, buffer + bufferWritePos);
        if (tailSize < size) {
            etl::copy(data + tailSize, data + size, buffer);
        }
        writePos += size;
        return size;
    }

    /**
     * @return TIndex Actually added data size.
     */
    TIndex
    Write(etl::span<const T> data)
    {
        return Write(data.data(), data.size());
    }

    /** Pop data from the buffer.
     * @return TIndex Number of actually obtained elements.
     */
    TIndex
    Read(T *out, TIndex size)
    {
        size = etl::min(size, GetSize());
        TIndex bufferReadPos = readPos & (capacity - 1);
        TIndex tailSize = etl::min<TIndex>(size, capacity - bufferReadPos);
        const T *readPtr = buffer + bufferReadPos;
        etl::copy(readPtr, readPtr + tailSize, out);
        if (tailSize < size) {
            etl::copy(buffer, buffer + size - tailSize, out + tailSize);
        }
        readPos += size;
        return size;
    }

    /** Get next available write region. Returns empty region if full. Write should be committed by
     * subsequent CommitWrite() call.
     */
    etl::span<T>
    GetWriteRegion()
    {
        TIndex bufferWritePos = writePos & (capacity - 1);
        TIndex size = etl::min<TIndex>(GetAvailableCapacity(), capacity - bufferWritePos);
        T *writePtr = buffer + bufferWritePos;
        return {writePtr, writePtr + size};
    }

    /** Commit write to a region previously obtained by GetWriteRegion(). */
    void
    CommitWrite(TIndex size)
    {
        PULSE_ASSERT(size <= etl::min<TIndex>(GetAvailableCapacity(),
                                              capacity - (writePos & (capacity - 1))));
        writePos += size;
    }

    /** Get next available read region. Returns empty region if empty. Read should be committed by
     * subsequent CommitRead() call.
     */
    etl::span<const T>
    GetReadRegion()
    {
        TIndex bufferReadPos = readPos & (capacity - 1);
        TIndex size = etl::min<TIndex>(GetSize(), capacity - bufferReadPos);
        T *readPtr = buffer + bufferReadPos;
        return {readPtr, readPtr + size};
    }

    /** Commit read to a region previously obtained by GetReadRegion(). */
    void
    CommitRead(TIndex size)
    {
        PULSE_ASSERT(size <= etl::min<TIndex>(GetSize(), capacity - (readPos & (capacity - 1))));
        readPos += size;
    }
private:
    TIndex writePos = 0, readPos = 0;
};


/** RingBuffer with embedded fixed size storage. */
template <typename T, size_t Capacity,
          etl::unsigned_integral TIndex = pulse::SizedUint<pulse::UintBitWidth(Capacity * 2)>>
class InlineRingBuffer: public RingBuffer<T, TIndex> {
public:
    InlineRingBuffer():
        RingBuffer<T, TIndex>(buffer, Capacity)
    {
        static_assert(Capacity * 2 <= etl::numeric_limits<TIndex>::max());
    }

private:
    T buffer[Capacity];
};

} // namespace pulse

#endif /* RING_BUFFER_H */
