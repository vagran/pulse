#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <pulse/details/common.h>
#include <etl/span.h>


namespace pulse {

namespace details {

template <typename T, size_t bufferSize, etl::unsigned_integral TIndex>
class RingBufferBase {
protected:
    etl::array<T, bufferSize> buffer;
    static constexpr TIndex size = bufferSize;

    static_assert(bufferSize > 0);
    static_assert(PULSE_IS_POW2(bufferSize));
    static_assert(bufferSize * 2 <= etl::numeric_limits<TIndex>::max());

    T *
    GetBuffer()
    {
        return buffer.data();
    }
};

template <typename T, etl::unsigned_integral TIndex>
class RingBufferBase<T, etl::dynamic_extent, TIndex> {
public:
    RingBufferBase() = delete;

    RingBufferBase(T *buffer, TIndex size):
        buffer(buffer),
        size(size)
    {
        PULSE_ASSERT(size > 0);
        PULSE_ASSERT(PULSE_IS_POW2(size));
        PULSE_ASSERT(size * 2 <= etl::numeric_limits<TIndex>::max());
    }

protected:
    T *buffer;
    const TIndex size;

    T *
    GetBuffer()
    {
        return buffer;
    }
};

} // namespace details


/** Light-weight ring buffer implementation. Buffer size should be strictly power of two to enable
 * fast math.
 * @tparam T Type of values to store.
 * @tparam TIndex Type of index field.
 */
template <typename T, size_t bufferSize = etl::dynamic_extent,
          etl::unsigned_integral TIndex = pulse::SizedUint<pulse::UintBitWidth(
            bufferSize == etl::dynamic_extent ? etl::dynamic_extent : bufferSize * 2)>>
class RingBuffer: public details::RingBufferBase<T, bufferSize, TIndex> {
public:
    using details::RingBufferBase<T, bufferSize, TIndex>::RingBufferBase;

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
        return this->size - GetSize();
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
        TIndex bufferWritePos = writePos & (this->size - 1);
        TIndex tailSize = etl::min<TIndex>(size, this->size - bufferWritePos);
        etl::copy(data, data + tailSize, this->GetBuffer() + bufferWritePos);
        if (tailSize < size) {
            etl::copy(data + tailSize, data + size, this->GetBuffer());
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
        TIndex bufferReadPos = readPos & (this->size - 1);
        TIndex tailSize = etl::min<TIndex>(size, this->size - bufferReadPos);
        const T *readPtr = this->GetBuffer() + bufferReadPos;
        etl::copy(readPtr, readPtr + tailSize, out);
        if (tailSize < size) {
            etl::copy(this->GetBuffer(), this->GetBuffer() + size - tailSize, out + tailSize);
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
        TIndex bufferWritePos = writePos & (this->size - 1);
        TIndex size = etl::min<TIndex>(GetAvailableCapacity(), this->size - bufferWritePos);
        T *writePtr = this->GetBuffer() + bufferWritePos;
        return {writePtr, writePtr + size};
    }

    /** Commit write to a region previously obtained by GetWriteRegion(). */
    void
    CommitWrite(TIndex size)
    {
        PULSE_ASSERT(size <= etl::min<TIndex>(GetAvailableCapacity(),
                                              this->size - (writePos & (this->size - 1))));
        writePos += size;
    }

    /** Get next available read region. Returns empty region if empty. Read should be committed by
     * subsequent CommitRead() call.
     */
    etl::span<const T>
    GetReadRegion()
    {
        TIndex bufferReadPos = readPos & (this->size - 1);
        TIndex size = etl::min<TIndex>(GetSize(), this->size - bufferReadPos);
        T *readPtr = this->GetBuffer() + bufferReadPos;
        return {readPtr, readPtr + size};
    }

    /** Commit read to a region previously obtained by GetReadRegion(). */
    void
    CommitRead(TIndex size)
    {
        PULSE_ASSERT(size <= etl::min<TIndex>(GetSize(),
                                              this->size - (readPos & (this->size - 1))));
        readPos += size;
    }

private:
    TIndex writePos = 0, readPos = 0;
};

} // namespace pulse

#endif /* RING_BUFFER_H */
