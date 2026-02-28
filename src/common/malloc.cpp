#include <pulse/malloc.h>
#include <pulse/details/common.h>
#include <etl/bit.h>
#include <etl/limits.h>
#include <stdint.h>


namespace {

static_assert(pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE <= sizeof(size_t),
              "pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE cannot be greater than size of size_t");

constexpr int ALLOC_UNIT_SHIFT =
    etl::countr_zero(static_cast<unsigned>(pulseConfig_MALLOC_GRANULARITY));

// Required for storing free list pointer.
static_assert(pulseConfig_MALLOC_ALIGNMENT >= alignof(void *),
              "pulseConfig_MALLOC_ALIGNMENT cannot be less than pointer alignment");


// Best matched uint type of given size.
template<size_t Bits>
using sized_uint_t = etl::conditional_t<
    Bits <= 8,  uint8_t,
    etl::conditional_t<Bits <= 16, uint16_t,
        etl::conditional_t<Bits <= 32, uint32_t,
            uint64_t>>>;


using BlockSizeUint = sized_uint_t<pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE * 8>;

// Convert size from allocation units to bytes.
constexpr size_t
GetAllocSize(BlockSizeUint numUnits)
{
    return static_cast<size_t>(numUnits) << ALLOC_UNIT_SHIFT;
}

struct BlockHeader {
    BlockSizeUint isFree:1,
                  blockSize:(sizeof(BlockSizeUint) * 8 - 1),
    // Last in region.
                  isLast:1,
    // Zero if the first block.
                  prevBlockSize:(sizeof(BlockSizeUint) * 8 - 1);
    // User data or FreeBlock. Cannot declare union here because alignment is ensured explicitly.
    // No padding needed here (and otherwise added by compiler if
    // `alignof(FreeBlock) > 2 * alignof(BlockSizeUint)` which is some extreme case like 32-bits
    // pointers with 1 byte pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE, but still supported).
    uint8_t data[];

    struct FreeBlock {
        BlockHeader *prevFree, *nextFree;

        /** @return True if first */
        void
        Unlink(BlockHeader *&head)
        {

        }
    };

    static BlockHeader *
    FromDataPtr(void *ptr)
    {
        return reinterpret_cast<BlockHeader *>(
            reinterpret_cast<uint8_t *>(ptr) - sizeof(BlockHeader));
    }

    inline bool
    HasPrev() const
    {
        return prevBlockSize != 0;
    }

    inline bool
    HasNext() const
    {
        return !isLast;
    }

    inline FreeBlock &
    GetFreeBlock()
    {
        PULSE_ASSERT(isFree);
        return *reinterpret_cast<FreeBlock *>(data);
    }

    // Get next properly aligned block address equal or greater than the specified one.
    static constexpr uintptr_t
    AlignUpBlockAddress(uintptr_t addr)
    {
        if constexpr (pulseConfig_MALLOC_ALIGNMENT > alignof(BlockHeader)) {
            // align data first, then take header address
            addr += sizeof(BlockHeader);
            addr = PULSE_ALIGN2(addr, pulseConfig_MALLOC_ALIGNMENT);
            addr -= sizeof(BlockHeader);
        } else {
            // just align the header
            addr = PULSE_ALIGN2(addr, alignof(BlockHeader));
        }
        return addr;
    }

    // Get previous properly aligned block address equal or less than the specified one.
    static constexpr uintptr_t
    AlignDownBlockAddress(uintptr_t addr)
    {
        if constexpr (pulseConfig_MALLOC_ALIGNMENT > alignof(BlockHeader)) {
            // align data first, then take header address
            addr += sizeof(BlockHeader);
            addr = PULSE_ALIGN2_DOWN(addr, pulseConfig_MALLOC_ALIGNMENT);
            addr -= sizeof(BlockHeader);
        } else {
            // just align the header
            addr = PULSE_ALIGN2_DOWN(addr, alignof(BlockHeader));
        }
        return addr;
    }

    inline BlockHeader *
    GetPrev() const
    {
        PULSE_ASSERT(prevBlockSize != 0);
        return reinterpret_cast<BlockHeader *>(AlignDownBlockAddress(
            reinterpret_cast<uintptr_t>(this) - GetAllocSize(prevBlockSize) - sizeof(BlockHeader)));
    }

    inline BlockHeader *
    GetNext() const
    {
        PULSE_ASSERT(!isLast);
        return reinterpret_cast<BlockHeader *>(AlignUpBlockAddress(
            reinterpret_cast<uintptr_t>(data) + GetAllocSize(blockSize)));
    }

    void
    Allocate(BlockHeader *&freeList)
    {
        PULSE_ASSERT(isFree);
        GetFreeBlock().Unlink(freeList);
        isFree = 0;
    }
};

consteval size_t
CalculateMaxAllocSize()
{
    // One bit is used for `free` flag.
    int blockSizeBits = sizeof(BlockSizeUint) * 8 - 1;
    int sizeTypeBits = sizeof(size_t) * 8;

    size_t numUnits;
    if (blockSizeBits + ALLOC_UNIT_SHIFT > sizeTypeBits) {
        numUnits = etl::numeric_limits<size_t>::max() >> ALLOC_UNIT_SHIFT;
    } else {
        numUnits = (static_cast<size_t>(1) << blockSizeBits) - 1;
    }
    return numUnits << ALLOC_UNIT_SHIFT;
}


constexpr size_t MAX_ALLOC_SIZE = CalculateMaxAllocSize();

constexpr size_t MIN_ALLOC_SIZE = PULSE_ALIGN2(sizeof(BlockHeader::FreeBlock), pulseConfig_MALLOC_GRANULARITY);

// Head of free blocks list.
BlockHeader *freeList = nullptr;

#ifdef pulseConfig_MALLOC_LOCK

#ifndef pulseConfig_MALLOC_UNLOCK
#error pulseConfig_MALLOC_LOCK defined while pulseConfig_MALLOC_UNLOCK is not defined
#endif

class MallocLockGuard {
public:
    MallocLockGuard(const MallocLockGuard &) = delete;

    MallocLockGuard()
    {
        pulseConfig_MALLOC_LOCK();
    }

    ~MallocLockGuard()
    {
        pulseConfig_MALLOC_UNLOCK();
    }
};

#define LockGuard() MallocLockGuard mallocLockGuard

#else // pulseConfig_MALLOC_LOCK

#ifdef pulseConfig_MALLOC_UNLOCK
#error pulseConfig_MALLOC_UNLOCK defined while pulseConfig_MALLOC_LOCK is not defined
#endif

#define LockGuard()

#endif // pulseConfig_MALLOC_LOCK

#if pulseConfig_MALLOC_STATS
// In allocation units.
size_t totalUsed = 0, totalFree = 0;

inline void
StatsAlloc(BlockHeader *block)
{
    totalUsed += block->blockSize;
}

// inline void
// StatsDealloc(BlockHeader *block)
// {
//     totalUsed -= block->blockSize;
// }

// inline void
// StatsFree(BlockHeader *block)
// {
//     totalFree += block->blockSize;
// }

inline void
StatsUnfree(BlockHeader *block)
{
    totalFree -= block->blockSize;
}

#else // pulseConfig_MALLOC_STATS

#define StatsAlloc(block)
#define StatsDealloc(block)
#define StatsFree(block)
#define StatsUnfree(block)

#endif // pulseConfig_MALLOC_STATS

} // anonymous namespace


void *
pulse_malloc(size_t size)
{
    if (size > MAX_ALLOC_SIZE) {
        return nullptr;
    }
    if (size < MIN_ALLOC_SIZE) {
        size = MIN_ALLOC_SIZE;
    }
    size_t numUnits = PULSE_ALIGN2(size, pulseConfig_MALLOC_GRANULARITY);

    LockGuard();

    if (!freeList) {
        return nullptr;
    }

    BlockHeader *p = freeList;
    BlockHeader *bestFit = nullptr;

    while (true) {
#if pulseConfig_MALLOC_BEST_FIT
        if (p->blockSize == numUnits) {
            bestFit = p;
            break;
        }
        if (p->blockSize > numUnits) {
            if (!bestFit || bestFit->blockSize > p->blockSize) {
                bestFit = p;
            }
        }
#else // pulseConfig_MALLOC_BEST_FIT
        if (p->blockSize >= numUnits) {
            bestFit = p;
            break;
        }
#endif // pulseConfig_MALLOC_BEST_FIT
        if (!p->HasNext()) {
            break;
        }
        p = p->GetNext();
    }

    if (bestFit) {
        bestFit->Allocate(freeList);
        StatsUnfree(bestFit);
        StatsAlloc(bestFit);
        return bestFit->data;
    }
    return nullptr;
}

void
pulse_free(void *ptr)
{

}

void *
pulse_realloc(void *ptr, size_t newSize)
{
    //XXX
    return nullptr;
}


void
pulse_add_heap_region(void *region, size_t size)
{
    // Null pointer are not supported as heap address. They are used to indicate invalid pointer in
    // free list.
    PULSE_ASSERT(region);
    //XXX
}
