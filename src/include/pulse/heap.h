#ifndef HEAP_H
#define HEAP_H

#include <pulse/details/common.h>
#include <etl/concepts.h>
#include <etl/memory.h>


namespace pulse {

template <typename F, typename T>
concept HeapComparator = requires(F f, const T &a, const T &b) {
    { f(a, b) } -> std::convertible_to<bool>;
};

template <typename T, auto IsHigher, size_t capacity>
requires HeapComparator<decltype(IsHigher), T>
class Heap {
public:
    ~Heap();

    static constexpr size_t
    Capacity()
    {
        return capacity;
    }

    size_t
    Size() const
    {
        return size;
    }

    bool
    IsEmpty() const
    {
        return size == 0;
    }

    /** @return True if inserted, false if capacity exceeded. */
    template <typename U>
    bool
    Insert(U &&item);

    /** Reference to top item. Valid only if not empty. Caller should not change the returned item
     * ordering.
     */
    T &
    Top()
    {
        return Item(0);
    }

    /** Reference to top item. Valid only if not empty. */
    const T &
    Top() const
    {
        return Item(0);
    }

    void
    PopTop();

private:
    struct alignas(T) {
        uint8_t buf[sizeof(T) * capacity];
    } data;

    size_t size = 0;

    T &
    Item(size_t idx)
    {
        PULSE_ASSERT(idx < size);
        return *reinterpret_cast<T *>(&data.buf[sizeof(T) * idx]);
    }

    const T &
    Item(size_t idx) const
    {
        PULSE_ASSERT(idx < size);
        return *reinterpret_cast<const T *>(&data.buf[sizeof(T) * idx]);
    }

    static constexpr size_t
    GetLChildIdx(size_t parentIdx)
    {
        return 2 * parentIdx + 1;
    }

    static constexpr size_t
    GetRChildIdx(size_t parentIdx)
    {
        return 2 * parentIdx + 2;
    }

    static constexpr size_t
    GetParentIdx(size_t childIdx)
    {
        return (childIdx - 1) / 2;
    }

    void
    SiftUp(size_t idx);

    void
    SiftDown(size_t idx);
};

template <typename T, auto IsHigher, size_t capacity>
requires HeapComparator<decltype(IsHigher), T>
Heap<T, IsHigher, capacity>::~Heap()
{
    for (size_t i = 0; i < size; i++) {
        etl::destroy_at(&Item(i));
    }
}

template <typename T, auto IsHigher, size_t capacity>
requires HeapComparator<decltype(IsHigher), T>
void
Heap<T, IsHigher, capacity>::SiftUp(size_t idx)
{
    while (idx > 0) {
        size_t parentIdx = GetParentIdx(idx);
        T &item = Item(idx);
        T &parent = Item(parentIdx);
        if (IsHigher(item, parent)) {
            etl::swap(item, parent);
            idx = parentIdx;
        } else {
            break;
        }
    }
}

template <typename T, auto IsHigher, size_t capacity>
requires HeapComparator<decltype(IsHigher), T>
void
Heap<T, IsHigher, capacity>::SiftDown(size_t idx)
{
    while (true) {
        size_t lChildIdx = GetLChildIdx(idx);
        if (lChildIdx >= size) {
            // Implies both are missing since lChildIdx < rChildIdx
            break;
        }
        size_t rChildIdx = GetRChildIdx(idx);
        size_t best = idx;
        if (IsHigher(Item(lChildIdx), Item(best))) {
            best = lChildIdx;
        }
        if (rChildIdx < size && IsHigher(Item(rChildIdx), Item(best))) {
            best = rChildIdx;
        }
        if (best == idx) {
            break;
        }
        etl::swap(Item(idx), Item(best));
        idx = best;
    }
}

template <typename T, auto IsHigher, size_t capacity>
requires HeapComparator<decltype(IsHigher), T>
template <typename U>
bool
Heap<T, IsHigher, capacity>::Insert(U &&item)
{
    if (size == capacity) {
        return false;
    }
    size_t idx = size;
    size++;
    new (&Item(idx)) T(etl::forward<U>(item));
    SiftUp(idx);
    return true;
}

template <typename T, auto IsHigher, size_t capacity>
requires HeapComparator<decltype(IsHigher), T>
void
Heap<T, IsHigher, capacity>::PopTop()
{
    size_t lastIdx = size - 1;
    Item(0) = etl::move(Item(lastIdx));
    etl::destroy_at(&Item(lastIdx));
    size--;
    SiftDown(0);
}

} // namespace pulse

#endif /* HEAP_H */
