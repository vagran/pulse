#include <pulse/malloc.h>
#include <pulse/details/common.h>
#include <etl/bit.h>
#include <etl/limits.h>
#include <stdint.h>
#include <string.h>

#if pulseConfig_MALLOC_DEBUG
#   include <etl/vector.h>
#endif


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
    //XXX make flag
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

        void
        Unlink(BlockHeader *&head)
        {
            if (prevFree) {
                prevFree->GetFreeBlock().nextFree = nextFree;
            } else {
                head = nextFree;
            }
            if (nextFree) {
                nextFree->GetFreeBlock().prevFree = prevFree;
            }
        }

        void
        Link(BlockHeader *&head)
        {
            prevFree = nullptr;
            nextFree = head;
            head = FromDataPtr(this);
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
    static BlockHeader *
    AlignUpBlockAddress(void *addr)
    {
        uintptr_t _addr = reinterpret_cast<uintptr_t>(addr);
        if constexpr (pulseConfig_MALLOC_ALIGNMENT > alignof(BlockHeader)) {
            // align data first, then take header address
            _addr += sizeof(BlockHeader);
            _addr = PULSE_ALIGN2(_addr, static_cast<uintptr_t>(pulseConfig_MALLOC_ALIGNMENT));
            _addr -= sizeof(BlockHeader);
        } else {
            // just align the header
            _addr = PULSE_ALIGN2(_addr, static_cast<uintptr_t>(alignof(BlockHeader)));
        }
        // Avoid integer-to-pointer cast which might be significant de-optimization at least in
        // Clang.
        return reinterpret_cast<BlockHeader *>(
            reinterpret_cast<uint8_t *>(addr) + (_addr - reinterpret_cast<uintptr_t>(addr)));
    }

    // Get previous properly aligned block address equal or less than the specified one.
    static BlockHeader *
    AlignDownBlockAddress(void *addr)
    {
        uintptr_t _addr = reinterpret_cast<uintptr_t>(addr);
        if constexpr (pulseConfig_MALLOC_ALIGNMENT > alignof(BlockHeader)) {
            // align data first, then take header address
            _addr += sizeof(BlockHeader);
            _addr = PULSE_ALIGN2_DOWN(_addr, static_cast<uintptr_t>(pulseConfig_MALLOC_ALIGNMENT));
            _addr -= sizeof(BlockHeader);
        } else {
            // just align the header
            _addr = PULSE_ALIGN2_DOWN(_addr, static_cast<uintptr_t>(alignof(BlockHeader)));
        }
        return reinterpret_cast<BlockHeader *>(
            reinterpret_cast<uint8_t *>(addr) + (_addr - reinterpret_cast<uintptr_t>(addr)));
    }

    inline BlockHeader *
    GetPrev()
    {
        PULSE_ASSERT(prevBlockSize != 0);
        return AlignDownBlockAddress(
            reinterpret_cast<uint8_t *>(this) - GetAllocSize(prevBlockSize) - sizeof(BlockHeader));
    }

    inline BlockHeader *
    GetNext()
    {
        PULSE_ASSERT(!isLast);
        return AlignUpBlockAddress(data + GetAllocSize(blockSize));
    }

    void
    Allocate(BlockHeader *&freeList)
    {
        PULSE_ASSERT(isFree);
        GetFreeBlock().Unlink(freeList);
        isFree = 0;
    }

    // Get block end address including padding before next block if any.
    uint8_t *
    GetEndAddress()
    {
        if (HasNext()) {
            return reinterpret_cast<uint8_t *>(GetNext());
        }
        return data + GetAllocSize(blockSize);
    }

    // Get full size including padding before next block if any.
    size_t
    GetSize()
    {
        return GetEndAddress() - data;
    }

    /** Split the block if possible.
     * @param dataSize Payload size to ensure in the first block.
     * @return Second block if split, null if was not possible.
     */
    BlockHeader *
    Split(size_t dataSize);

    void
    Merge(BlockHeader *next)
    {
        PULSE_ASSERT(next == GetNext());
        isLast = next->isLast;
        uint8_t *endAddr = next->GetEndAddress();
        blockSize = (endAddr - data) >> ALLOC_UNIT_SHIFT;
        if (HasNext()) {
            GetNext()->prevBlockSize = blockSize;
            PULSE_ASSERT(GetNext()->GetPrev() == this);
        }
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

constexpr size_t MIN_ALLOC_SIZE =
    PULSE_ALIGN2(sizeof(BlockHeader::FreeBlock), pulseConfig_MALLOC_GRANULARITY);

BlockHeader *
BlockHeader::Split(size_t dataSize)
{
    uint8_t *end = GetEndAddress();
    BlockHeader *next = AlignUpBlockAddress(
        data + PULSE_ALIGN2(dataSize, pulseConfig_MALLOC_GRANULARITY));
    if (next->data + MIN_ALLOC_SIZE > end) {
        return nullptr;
    }
    next->blockSize = (end - next->data) >> ALLOC_UNIT_SHIFT;
    next->isFree = 1;
    next->isLast = isLast;
    blockSize = (reinterpret_cast<uint8_t *>(next) - data) >> ALLOC_UNIT_SHIFT;
    next->prevBlockSize = blockSize;
    isLast = 0;
    PULSE_ASSERT(next->GetPrev() == this);
    PULSE_ASSERT(GetNext() == next);
    return next;
}

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

inline void
StatsDealloc(BlockHeader *block)
{
    totalUsed -= block->blockSize;
}

inline void
StatsFree(BlockHeader *block)
{
    totalFree += block->blockSize;
}

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

#ifdef pulseConfig_MALLOC_FREE_SPACE_POISONING

static_assert(pulseConfig_MALLOC_FREE_SPACE_POISONING <= etl::numeric_limits<uint8_t>::max(),
             "pulseConfig_MALLOC_FREE_SPACE_POISONING out of range");

void
PoisonFreeBlock(BlockHeader *block)
{
    PULSE_ASSERT(block->isFree);
    memset(block->data + sizeof(BlockHeader::FreeBlock), pulseConfig_MALLOC_FREE_SPACE_POISONING,
           block->GetSize() - sizeof(BlockHeader::FreeBlock));
}

#else // pulseConfig_MALLOC_FREE_SPACE_POISONING

#define PoisonFreeBlock(block)

#endif // pulseConfig_MALLOC_FREE_SPACE_POISONING

// Should be called with lock acquired.
BlockHeader *
AllocateBlock(size_t size)
{
    PULSE_ASSERT(size >= MIN_ALLOC_SIZE);
    PULSE_ASSERT(size <= MAX_ALLOC_SIZE);

    size_t numUnits PULSE_UNUSED = PULSE_ALIGN2(size, pulseConfig_MALLOC_GRANULARITY);//XXX shift

    if (!freeList) {
        return nullptr;
    }

    BlockHeader *p = freeList;
    BlockHeader *bestFit = nullptr;

    while (p) {
        PULSE_ASSERT(p->isFree);
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
        if (p->GetSize() >= size) {
            bestFit = p;
            break;
        }
#endif // pulseConfig_MALLOC_BEST_FIT
        p = p->GetFreeBlock().nextFree;
    }

    if (bestFit) {
        bestFit->Allocate(freeList);
        StatsUnfree(bestFit);

        BlockHeader *next = bestFit->Split(size);
        if (next) {
            next->GetFreeBlock().Link(freeList);
            StatsFree(next);
        }

        StatsAlloc(bestFit);
        return bestFit;
    }

    return nullptr;
}

// Should be called with lock acquired.
void
FreeBlock(BlockHeader *block)
{
    if (block->HasNext()) {
        BlockHeader *next = block->GetNext();
        if (next->isFree) {
            uint8_t *endAddr = next->GetEndAddress();
            if (endAddr - block->data <= MAX_ALLOC_SIZE) {
                // Merge with the next block
                StatsUnfree(next);
                next->GetFreeBlock().Unlink(freeList);
                block->Merge(next);
            }
        }
    }

    if (block->HasPrev()) {
        BlockHeader *prev = block->GetPrev();
        if (prev->isFree) {
            uint8_t *endAddr = block->GetEndAddress();
            if (endAddr - prev->data <= MAX_ALLOC_SIZE) {
                // Merge with the previous block
                StatsUnfree(prev);
                prev->GetFreeBlock().Unlink(freeList);
                prev->Merge(block);
                block = prev;
            }
        }
    }

    block->isFree = 1;
    StatsFree(block);
    PoisonFreeBlock(block);
    block->GetFreeBlock().Link(freeList);
}

#if pulseConfig_MALLOC_DEBUG

struct HeapRegion {
    uint8_t *ptr;
    size_t size;

    BlockHeader *
    GetFirstBlock() const
    {
        BlockHeader *block = BlockHeader::AlignUpBlockAddress(ptr);
        ptrdiff_t blockOffset = reinterpret_cast<uint8_t *>(block) - ptr;
        if (blockOffset + sizeof(BlockHeader) + MIN_ALLOC_SIZE > size) {
            return nullptr;
        }
        return block;
    }

    bool
    ContainsBlock(BlockHeader *block)
    {
        uint8_t *end = block->GetEndAddress();
        return reinterpret_cast<uint8_t *>(block) >= ptr || end <= ptr + size;
    }

    bool
    IsLast(BlockHeader *block)
    {
        PULSE_ASSERT(block->isLast);
        uint8_t *end = block->GetEndAddress();
        BlockHeader *next = BlockHeader::AlignUpBlockAddress(end);
        ptrdiff_t blockOffset = reinterpret_cast<uint8_t *>(next) - end;
        size_t size = ptr + this->size - end;
        return blockOffset + sizeof(BlockHeader) + MIN_ALLOC_SIZE > size;
    }
};

etl::vector<HeapRegion, 8> heapRegions;

void
DebugRegisterHeapRegion(void *region, size_t size)
{
    if (heapRegions.full()) {
        return;
    }
    heapRegions.emplace_back(HeapRegion{reinterpret_cast<uint8_t *>(region), size});
}

#define DEBUG_ASSERT(x) do { \
    bool _x = (x); \
    PULSE_ASSERT(_x && PULSE_STR(x)); \
    if (!_x) { \
        return false; \
    } \
} while (false)

#else  // pulseConfig_MALLOC_DEBUG

#define DebugRegisterHeapRegion(region, size)

#endif // pulseConfig_MALLOC_DEBUG

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

    LockGuard();

    BlockHeader *block = AllocateBlock(size);

    if (!block) {
        return nullptr;
    }
    return block->data;
}

void
pulse_free(void *ptr)
{
    BlockHeader *block = BlockHeader::FromDataPtr(ptr);
    PULSE_ASSERT(!block->isFree);

    LockGuard();

    StatsDealloc(block);

    FreeBlock(block);
}

void *
pulse_realloc(void *ptr, size_t newSize)
{
    if (!ptr) {
        return pulse_malloc(newSize);
    }

    if (newSize > MAX_ALLOC_SIZE) {
        return nullptr;
    }
    if (newSize < MIN_ALLOC_SIZE) {
        newSize = MIN_ALLOC_SIZE;
    }

    BlockHeader *block = BlockHeader::FromDataPtr(ptr);
    PULSE_ASSERT(!block->isFree);

    LockGuard();

    size_t curSize = block->GetSize();

    if (newSize <= curSize) {
        // Trim this block if size is significantly shrunk.
        if (newSize <= curSize / 2) {
            StatsDealloc(block);
            BlockHeader *next = block->Split(newSize);
            if (next) {
                StatsFree(next);
                PoisonFreeBlock(next);
                next->GetFreeBlock().Link(freeList);
            }
            StatsAlloc(block);
        }
        return block->data;
    }

    BlockHeader *next = nullptr;
    uint8_t *nextEndAddr = nullptr;
    bool wantPrev = true;
    if (block->HasNext()) {
        next = block->GetNext();
        if (next->isFree) {
            nextEndAddr = next->GetEndAddress();
            if (nextEndAddr - block->data > MAX_ALLOC_SIZE) {
                // Disable merging with next if too big resulted block
                nextEndAddr = nullptr;
            } else {
                // Require previous block only if still not enough
                wantPrev = nextEndAddr - block->data < newSize;
            }
        }
    }

    BlockHeader *prev = nullptr;
    if (wantPrev && block->HasPrev()) {
        prev = block->GetPrev();
        if (prev->isFree) {
            size_t size = block->GetEndAddress() - prev->data;
            if (size > MAX_ALLOC_SIZE) {
                prev = nullptr;
            } if (size >= newSize) {
                // Merging with previous block is enough.
                nextEndAddr = nullptr;
            } else if (nextEndAddr) {
                // Check merging with both neighbors
                size = nextEndAddr - prev->data;
                if (size > MAX_ALLOC_SIZE || size < newSize) {
                    nextEndAddr = nullptr;
                    prev = nullptr;
                }
            } else {
                // Not enough space
                prev = nullptr;
            }
        } else {
            // Not free
            prev = nullptr;
        }
    }

    if (!nextEndAddr && !prev) {
        // No merging, try allocating new block and move there.
        BlockHeader *newBlock = AllocateBlock(newSize);
        if (!newBlock) {
            return nullptr;
        }
        memcpy(newBlock->data, block->data, curSize);
        StatsDealloc(block);
        FreeBlock(block);
        return newBlock->data;
    }

    // Merge with one or both neighbors
    StatsDealloc(block);

    if (nextEndAddr) {
        StatsUnfree(next);
        next->GetFreeBlock().Unlink(freeList);
        block->Merge(next);
        if (!prev) {
            StatsAlloc(block);
            return block->data;
        }
    }

    StatsUnfree(prev);
    prev->GetFreeBlock().Unlink(freeList);
    prev->Merge(block);
    memcpy(prev->data, block->data, curSize);
    StatsAlloc(prev);
    return prev->data;
}

void
pulse_add_heap_region(void *region, size_t size)
{
    // Null pointer are not supported as heap address. They are used to indicate invalid pointer in
    // free list.
    PULSE_ASSERT(region);

    LockGuard();

    DebugRegisterHeapRegion(region, size);

    uint8_t *addr = reinterpret_cast<uint8_t *>(region);
    uint8_t *end = addr + size;
    BlockHeader *prevBlock = nullptr;

    while (true) {
        BlockHeader *block = BlockHeader::AlignUpBlockAddress(addr);
        if (block->data + MIN_ALLOC_SIZE > end) {
            break;
        }
        if (prevBlock) {
            // Can do if next block is available otherwise isLast must be set
            PoisonFreeBlock(prevBlock);
        }
        size_t dataSize = end - block->data;
        if (dataSize > MAX_ALLOC_SIZE) {
            dataSize = MAX_ALLOC_SIZE;
        }
        block->isFree = 1;
        block->blockSize = dataSize >> ALLOC_UNIT_SHIFT;
        block->isLast = 0;
        block->prevBlockSize = prevBlock ? prevBlock->blockSize : 0;
        StatsFree(block);
        block->GetFreeBlock().Link(freeList);
        prevBlock = block;
        addr = block->data + GetAllocSize(block->blockSize);
        PULSE_ASSERT(addr <= end);
    }

    if (prevBlock) {
        prevBlock->isLast = 1;
        PoisonFreeBlock(prevBlock);
    }
}

size_t
get_malloc_max_size()
{
    return MAX_ALLOC_SIZE;
}

#if pulseConfig_MALLOC_STATS

void
get_malloc_stats(MallocStats *stats)
{
    stats->totalFree = totalFree << ALLOC_UNIT_SHIFT;
    stats->totalUsed = totalUsed << ALLOC_UNIT_SHIFT;
}

#endif // pulseConfig_MALLOC_STATS

#if pulseConfig_MALLOC_DEBUG

bool
validate_heap()
{
    for (HeapRegion &region: heapRegions) {
        BlockHeader *block = region.GetFirstBlock();
        BlockHeader *prevBlock = nullptr;
        while (block) {
            DEBUG_ASSERT(region.ContainsBlock(block));
            if (prevBlock) {
                DEBUG_ASSERT(block->GetPrev() == prevBlock);
                DEBUG_ASSERT(block->prevBlockSize == prevBlock->blockSize);
            }
            if (!block->HasNext()) {
                break;
            }
            prevBlock = block;
            block = block->GetNext();
        }
        //XXX
        // if (block) {
        //     DEBUG_ASSERT(region.IsLast(block));
        // }
    }

    BlockHeader *p = freeList;
    BlockHeader *prevBlock = nullptr;
    while (p) {
        DEBUG_ASSERT(p->isFree);
        DEBUG_ASSERT(p->GetFreeBlock().prevFree == prevBlock);
        p = p->GetFreeBlock().nextFree;
    }

    return true;
}

#endif // pulseConfig_MALLOC_DEBUG
