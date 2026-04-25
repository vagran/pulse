#ifndef HEAP_H
#define HEAP_H

#include <pulse/details/common.h>
#include <etl/concepts.h>
#include <etl/memory.h>


namespace pulse {

template <typename T, typename F>
concept HeapComparator = requires(F f, const T &a, const T &b) {
    { f(a, b) } -> etl::convertible_to<bool>;
};

template <typename T, typename F>
concept HeapItemSetIndex = requires(F f, T &item, size_t index) {
    { f(item, index) } -> etl::same_as<void>;
};

namespace details {

template <typename T>
inline void
DefaultHeapItemSetIndex(T &, size_t)
{}

} // namespace details

template <typename T, auto IsHigher, size_t capacity,
          auto ItemSetIndex = details::DefaultHeapItemSetIndex<T>>
requires HeapComparator<T, decltype(IsHigher)> &&
         HeapItemSetIndex<T, decltype(ItemSetIndex)>
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

    /** Remove item from the specified position. */
    void
    Remove(size_t index);

private:
    struct alignas(T) {
        uint8_t buf[sizeof(T) * capacity];
    } data;

    size_t size = 0;

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
        PULSE_ASSERT(childIdx > 0);
        return (childIdx - 1) / 2;
    }

    void
    SiftUp(size_t idx);

    void
    SiftDown(size_t idx);
};

template <typename T, auto IsHigher, size_t capacity, auto ItemSetIndex>
requires HeapComparator<T, decltype(IsHigher)> &&
         HeapItemSetIndex<T, decltype(ItemSetIndex)>
Heap<T, IsHigher, capacity, ItemSetIndex>::~Heap()
{
    for (size_t i = 0; i < size; i++) {
        etl::destroy_at(&Item(i));
    }
}

template <typename T, auto IsHigher, size_t capacity, auto ItemSetIndex>
requires HeapComparator<T, decltype(IsHigher)> &&
         HeapItemSetIndex<T, decltype(ItemSetIndex)>
void
Heap<T, IsHigher, capacity, ItemSetIndex>::SiftUp(size_t idx)
{
    while (idx > 0) {
        size_t parentIdx = GetParentIdx(idx);
        T &item = Item(idx);
        T &parent = Item(parentIdx);
        if (IsHigher(item, parent)) {
            etl::swap(item, parent);
            ItemSetIndex(item, idx);
            ItemSetIndex(parent, parentIdx);
            idx = parentIdx;
        } else {
            break;
        }
    }
}

template <typename T, auto IsHigher, size_t capacity, auto ItemSetIndex>
requires HeapComparator<T, decltype(IsHigher)> &&
         HeapItemSetIndex<T, decltype(ItemSetIndex)>
void
Heap<T, IsHigher, capacity, ItemSetIndex>::SiftDown(size_t idx)
{
    while (true) {
        size_t lChildIdx = GetLChildIdx(idx);
        if (lChildIdx >= size) {
            // Implies both are missing since lChildIdx < rChildIdx
            break;
        }
        size_t rChildIdx = GetRChildIdx(idx);
        size_t bestIdx = idx;
        if (IsHigher(Item(lChildIdx), Item(bestIdx))) {
            bestIdx = lChildIdx;
        }
        if (rChildIdx < size && IsHigher(Item(rChildIdx), Item(bestIdx))) {
            bestIdx = rChildIdx;
        }
        if (bestIdx == idx) {
            break;
        }
        T &item = Item(idx);
        T &bestItem = Item(bestIdx);
        etl::swap(item, bestItem);
        ItemSetIndex(item, idx);
        ItemSetIndex(bestItem, bestIdx);
        idx = bestIdx;
    }
}

template <typename T, auto IsHigher, size_t capacity, auto ItemSetIndex>
requires HeapComparator<T, decltype(IsHigher)> &&
         HeapItemSetIndex<T, decltype(ItemSetIndex)>
template <typename U>
bool
Heap<T, IsHigher, capacity, ItemSetIndex>::Insert(U &&item)
{
    if (size == capacity) {
        return false;
    }
    size_t idx = size;
    size++;
    T &_item = Item(idx);
    new (&_item) T(etl::forward<U>(item));
    ItemSetIndex(_item, idx);
    SiftUp(idx);
    return true;
}

template <typename T, auto IsHigher, size_t capacity, auto ItemSetIndex>
requires HeapComparator<T, decltype(IsHigher)> &&
         HeapItemSetIndex<T, decltype(ItemSetIndex)>
void
Heap<T, IsHigher, capacity, ItemSetIndex>::PopTop()
{
    PULSE_ASSERT(size);
    size_t lastIdx = size - 1;
    T &lastItem = Item(lastIdx);
    if (lastIdx != 0) {
        Item(0) = etl::move(lastItem);
        ItemSetIndex(Item(0), 0);
    }
    etl::destroy_at(&lastItem);
    size--;
    SiftDown(0);
}

template <typename T, auto IsHigher, size_t capacity, auto ItemSetIndex>
requires HeapComparator<T, decltype(IsHigher)> &&
         HeapItemSetIndex<T, decltype(ItemSetIndex)>
void
Heap<T, IsHigher, capacity, ItemSetIndex>::Remove(size_t index)
{
    PULSE_ASSERT(index < size);
    size_t lastIdx = size - 1;
    T &lastItem = Item(lastIdx);
    if (index == lastIdx) {
        etl::destroy_at(&lastItem);
        size--;
        return;
    }
    T &item = Item(index);
    bool requiresSiftUp = IsHigher(lastItem, item);
    item = etl::move(lastItem);
    ItemSetIndex(item, index);
    etl::destroy_at(&lastItem);
    size--;
    if (requiresSiftUp) {
        SiftUp(index);
    } else {
        SiftDown(index);
    }
}

} // namespace pulse

#endif /* HEAP_H */
