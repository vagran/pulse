/* This file is compiled with differrent combinations of malloc parameters (alignment, granularity,
 * size word size).
 */

#include <catch2/catch_test_macros.hpp>
#include <common.h>
#include <pulse/malloc.h>
#include <unordered_set>
#include <random>
#include <iostream>


namespace {

void
CheckAligned(void *ptr)
{
    constexpr uintptr_t mask = static_cast<uintptr_t>(pulseConfig_MALLOC_GRANULARITY) - 1;
    REQUIRE((reinterpret_cast<uintptr_t>(ptr) & mask) == 0);
}

bool
IsFilled(uint8_t *ptr, size_t size, uint8_t value)
{
    for (size_t i = 0; i < size; i++) {
        if (ptr[i] != value) [[unlikely]] {
            return false;
        }
    }
    return true;
}

struct Block {
    size_t size;
    uint8_t fill;
    uint8_t *ptr;

    Block(size_t size, uint8_t fill):
        size(size),
        fill(fill),
        ptr(reinterpret_cast<uint8_t *>(pulse_malloc(size)))
    {
        if (ptr) {
            CheckInHeap(ptr, size);
            CheckAligned(ptr);
            std::memset(ptr, fill, size);
        }
    }

    operator bool() const
    {
        return ptr != nullptr;
    }

    void
    CheckFill() const
    {
        if (ptr) {
            REQUIRE(IsFilled(ptr, size, fill));
        }
    }

    void
    Free()
    {
        if (ptr) {
            pulse_free(ptr);
            ptr = nullptr;
        }
    }

    bool
    Realloc(size_t size, uint8_t fill)
    {
        void *newPtr = pulse_realloc(ptr, size);
        if (!newPtr) {
            return false;
        }
        ptr = reinterpret_cast<uint8_t *>(newPtr);
        if (size < this->size) {
            this->size = size;
        }
        CheckInHeap(ptr, size);
        CheckAligned(ptr);
        CheckFill();
        this->size = size;
        this->fill = fill;
        std::memset(ptr, fill, size);
        return true;
    }

    bool
    operator==(const Block& other) const
    {
        return ptr == other.ptr;
    }

    struct Compare {
        bool
        operator()(const Block& lhs, const Block& rhs) const
        {
            return lhs.ptr < rhs.ptr;
        }
    };

    struct Hash {
        size_t
        operator()(const Block& block) const
        {
            return std::hash<decltype(block.ptr)>{}(block.ptr);
        }
    };

};

class Context {
public:
    std::unordered_set<Block, Block::Hash> blocks;
    const size_t maxAllocSize;
    uint32_t seed;
    std::mt19937 rng;
    std::uniform_int_distribution<size_t> sizeDist{1, maxAllocSize};
    std::uniform_int_distribution<uint8_t> fillDist{0, 0xff};
    size_t totalAllocs = 0, totalReallocs = 0, totalFrees = 0;
    size_t numFailedAllocs = 0, numFailedReallocs = 0;

    Context(size_t maxAllocSize, uint32_t seed = 0):
        maxAllocSize(maxAllocSize),
        seed(seed ? seed : std::random_device()()),
        rng(this->seed)
    {
        std::cout << "Test random seed: " << this->seed << "\n";
    }

    bool
    IsEmpty() const
    {
        return blocks.empty();
    }

    size_t
    GetRandomSize()
    {
        return sizeDist(rng);
    }

    uint8_t
    GetRandomFill()
    {
        return fillDist(rng);
    }

    const Block &
    GetRandomBlock()
    {
        REQUIRE(!blocks.empty());
        std::uniform_int_distribution<size_t> dist(0, blocks.size() - 1);
        size_t targetIdx = dist(rng);
        auto it = blocks.begin();
        for (size_t idx = 0; idx != targetIdx; it++, idx++);
        return *it;
    }

    void
    CheckAllFills()
    {
        for (const Block &block: blocks) {
            block.CheckFill();
        }
    }

    const Block *
    Allocate()
    {
        totalAllocs++;
        Block block(GetRandomSize(), GetRandomFill());
        if (!block) {
            numFailedAllocs++;
            return nullptr;
        }
        auto res = blocks.emplace(block);
        REQUIRE(res.second);
        return &*res.first;
    }

    void
    Free(Block &block)
    {
        totalFrees++;
        if (block.ptr) {
            REQUIRE(blocks.erase(block) == 1);
        }
        block.Free();
    }

    const Block *
    Reallocate(Block &block)
    {
        totalReallocs++;
        if (block) {
            REQUIRE(blocks.erase(block) == 1);
        }
        if (!block.Realloc(GetRandomSize(), GetRandomFill())) {
            numFailedReallocs++;
        }
        if (block) {
            auto res = blocks.emplace(block);
            REQUIRE(res.second);
            return &*res.first;
        }
        return nullptr;
    }
};

} // anonymous namespace

#ifndef M_ALLOC_RATIO
#define M_ALLOC_RATIO 64
#endif


TEST_CASE("Random activity")
{
    std::cout << "M_GRAN=" << M_GRAN << " M_BSZ=" << M_BSZ << " M_FIT=" << M_FIT << "\n";

    size_t maxAllocSize = GetHeapSize() / M_ALLOC_RATIO;
    Context ctx(maxAllocSize);
    std::uniform_int_distribution<uint8_t> actionDist{0, 2};

    InitHeap();
    validate_heap();

    MallocStats stats;
    get_malloc_stats(&stats);

    std::cout << "Total free before tests: " << stats.totalFree << "\n";
    std::cout << "Maximal allocation size: " << maxAllocSize << ", allocator limit: " <<
        get_malloc_max_size() << "\n";

    size_t numInitialAllocations = std::max(M_ALLOC_RATIO * 2, 1000);
    for (int i = 0; i < numInitialAllocations; i++) {
        ctx.Allocate();
        ctx.CheckAllFills();
        validate_heap();
        if ((i & 1023) == 0) {
            get_malloc_stats(&stats);
            if (stats.totalFree < maxAllocSize * 2) {
                std::cout << "Interrupting initial allocation due to heap depletion after " <<
                    i + 1<< " iterations\n";
                break;
            }
        }
    }

    get_malloc_stats(&stats);
    std::cout << "Total free after initial allocations: " << stats.totalFree << ", min free: " <<
        stats.minFree << ", total used: " << stats.totalUsed << "\n";
    std::cout << "Failed allocations: " << ctx.numFailedAllocs << "\n";
    std::cout << "Number of allocated blocks: " << ctx.blocks.size() << "\n";

    for (int i = 0; i < 10000; i++) {
        uint8_t action = actionDist(ctx.rng);
        switch (action) {
        case 0:
            ctx.Allocate();
            break;
        case 1: {
            if (!ctx.IsEmpty()) {
                Block block = ctx.GetRandomBlock();
                ctx.Free(block);
            }
            break;
        }
        case 2: {
            if (!ctx.IsEmpty()) {
                Block block = ctx.GetRandomBlock();
                ctx.Reallocate(block);
            }
            break;
        }
        default:
            FAIL("should not be reached");
        }
        ctx.CheckAllFills();
        validate_heap();
    }

    get_malloc_stats(&stats);
    std::cout << "Total free after mixed actions: " << stats.totalFree << ", min free: " <<
        stats.minFree << ", total used: " << stats.totalUsed << "\n";
    std::cout << "Total allocs: " << ctx.totalAllocs << ", reallocs: " << ctx.totalReallocs <<
        ", frees: " << ctx.totalFrees << "\n";
    std::cout << "Failed allocations: " << ctx.numFailedAllocs << ", failed re-allocs: " <<
        ctx.numFailedReallocs << "\n";
    std::cout << "Number of allocated blocks: " << ctx.blocks.size() << "\n";

    while (!ctx.IsEmpty()) {
        Block block = ctx.GetRandomBlock();
        ctx.Free(block);
        ctx.CheckAllFills();
        validate_heap();
    }

    get_malloc_stats(&stats);
    REQUIRE(stats.totalUsed == 0);
    std::cout << "Total free after test: " << stats.totalFree << ", min free: " << stats.minFree <<
        ", total used: " << stats.totalUsed << "\n";
}
