#ifndef PULSE_POOL_H
#define PULSE_POOL_H

#include <pulse/details/common.h>
#include <etl/memory.h>


namespace pulse {

namespace details {

struct PoolNopGuard {};

template <typename Tr>
concept PoolTrait = requires {
    // static void OnAllocated();
    // Called after each allocation (either from pool or dynamic).
    { Tr::OnAllocated() } -> etl::same_as<void>;

    // static void OnPoolAllocated();
    // Called after each allocation from pool.
    { Tr::OnPoolAllocated() } -> etl::same_as<void>;

    // static void OnDynamicallyAllocated();
    // Called after each dynamic allocation.
    { Tr::OnDynamicallyAllocated() } -> etl::same_as<void>;

    // static void OnFreed();
    // Called after each freeing.
    { Tr::OnFreed() } -> etl::same_as<void>;
};

struct DefaultPoolTrait {
    static void
    OnAllocated()
    {}

    static void
    OnPoolAllocated()
    {}

    static void
    OnDynamicallyAllocated()
    {}

    static void
    OnFreed()
    {}
};

} // namespace details

/// Pool for fixed-size objects. Can be optionally extended by dynamic allocations.
/// @tparam allowDynamicAlloc Allow dynamic allocation from heap if true. The allocated item freed
///     into the pool.
/// @tparam TGuard Context object to guard allocation and freeing. May be some kind of
///     synchronization primitive.
/// @tparam Trait Can provide some customization hooks. See @ref details::PoolTrait.
template <typename T, size_t initialSize, bool allowDynamicAlloc = false,
          class TGuard = details::PoolNopGuard,
          details::PoolTrait Trait = details::DefaultPoolTrait>
class Pool {
private:
    struct Entry {
        union {
            // Next entry when in free list.
            Entry *next;
            T item;
        };

        Entry()
        {
            // Union members are constructed explicitly in Allocate/Free
        }

        ~Entry()
        {
            // Union members are destroyed explicitly in Free. Required so that Entry is
            // destructible even when T has a non-trivial destructor.
        }
    };

    Entry pool[initialSize];
    Entry *freeList = nullptr;

public:
    Pool();

    ~Pool();

    template <typename... TArgs>
    T *
    Allocate(TArgs &&... args);

    void
    Free(T *item);

    /// Does not call item constructor. For use with custom `new` operator.
    void *
    New(size_t size);

    /// Does not call item destructor. For use with custom `delete` operator.
    void
    Delete(void *mem, size_t size);
};


template <typename T, size_t initialSize, bool allowDynamicAlloc, class TGuard,
          details::PoolTrait Trait>
Pool<T, initialSize, allowDynamicAlloc, TGuard, Trait>::Pool()
{
    for (size_t i = 0; i < initialSize; i++) {
        if (i < initialSize - 1) {
            pool[i].next = &pool[i + 1];
        } else {
            pool[i].next = nullptr;
        }
    }
    if (initialSize > 0) {
        freeList = &pool[0];
    }
}

// Mostly for clean unit tests. Global objects destructors are stripped by linker when compiling
// firmware.
template <typename T, size_t initialSize, bool allowDynamicAlloc, class TGuard,
          details::PoolTrait Trait>
Pool<T, initialSize, allowDynamicAlloc, TGuard, Trait>::~Pool()
{
    Entry *e = freeList;
    while (e) {
        Entry *next = e->next;
        if (e < pool || e >= pool + initialSize) {
            delete e;
        }
        e = next;
    }
}

template <typename T, size_t initialSize, bool allowDynamicAlloc, class TGuard,
          details::PoolTrait Trait>
template <typename... TArgs>
T *
Pool<T, initialSize, allowDynamicAlloc, TGuard, Trait>::Allocate(TArgs &&... args)
{
    T *item = reinterpret_cast<T *>(New(sizeof(T)));
    if (!item) {
        return nullptr;
    }
    etl::construct_at(item, etl::forward<TArgs>(args)...);
    return item;
}

template <typename T, size_t initialSize, bool allowDynamicAlloc, class TGuard,
          details::PoolTrait Trait>
void
Pool<T, initialSize, allowDynamicAlloc, TGuard, Trait>::Free(T *item)
{
    etl::destroy_at(item);
    Delete(item, sizeof(T));
}

template <typename T, size_t initialSize, bool allowDynamicAlloc, class TGuard,
          details::PoolTrait Trait>
void *
Pool<T, initialSize, allowDynamicAlloc, TGuard, Trait>::New(size_t size)
{
    PULSE_ASSERT(size == sizeof(T));
    Entry *e = nullptr;
    {
        [[maybe_unused]] TGuard guard;
        if (freeList) {
            e = freeList;
            freeList = e->next;
        }
    }
    if (e) {
        Trait::OnPoolAllocated();
    } else {
        if constexpr (allowDynamicAlloc) {
            e = new Entry();
            if (!e) {
                return nullptr;
            }
        } else {
            return nullptr;
        }
        Trait::OnDynamicallyAllocated();
    }
    Trait::OnAllocated();
    return e;
}

template <typename T, size_t initialSize, bool allowDynamicAlloc, class TGuard,
          details::PoolTrait Trait>
void
Pool<T, initialSize, allowDynamicAlloc, TGuard, Trait>::Delete(void *mem, size_t size)
{
    PULSE_ASSERT(size == sizeof(T));
    Entry *e = reinterpret_cast<Entry *>(mem);
    {
        [[maybe_unused]] TGuard guard;
        e->next = freeList;
        freeList = e;
    }
    Trait::OnFreed();
}

} // namespace pulse

#endif /* PULSE_POOL_H */
