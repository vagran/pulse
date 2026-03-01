/* This file is compiled with differrent combinations of malloc parameters (alignment, granularity,
 * size word size).
 */

#include <catch2/catch_test_macros.hpp>
#include <common.h>
#include <pulse/malloc.h>
#include <set>
#include <random>
#include <iostream>


namespace {

void
CheckAligned(void *ptr)
{
    constexpr uintptr_t mask = static_cast<uintptr_t>(pulseConfig_MALLOC_ALIGNMENT) - 1;
    REQUIRE((reinterpret_cast<uintptr_t>(ptr) & mask) == 0);
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
            for (size_t i = 0; i < size; i++) {
                CHECK(ptr[i] == fill);
            }
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

    struct Compare {
        bool
        operator()(const Block& lhs, const Block& rhs) const
        {
            return lhs.ptr < rhs.ptr;
        }
    };
};

class Context {
public:
    std::set<Block, Block::Compare> blocks;
    const size_t maxAllocSize;
    uint32_t seed;
    std::mt19937 rng;
    std::uniform_int_distribution<size_t> sizeDist{1, maxAllocSize};
    std::uniform_int_distribution<uint8_t> fillDist{0, 0xff};

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
        Block block(GetRandomSize(), GetRandomFill());
        if (!block) {
            return nullptr;
        }
        auto res = blocks.emplace(block);
        REQUIRE(res.second);
        return &*res.first;
    }

    void
    Free(Block &block)
    {
        if (block.ptr) {
            REQUIRE(blocks.erase(block) == 1);
        }
        block.Free();
    }

    const Block *
    Reallocate(Block &block)
    {
        if (block) {
            REQUIRE(blocks.erase(block) == 1);
        }
        block.Realloc(GetRandomSize(), GetRandomFill());
        if (block) {
            auto res = blocks.emplace(block);
            REQUIRE(res.second);
            return &*res.first;
        }
        return nullptr;
    }
};

}

TEST_CASE("Random activity") {
    Context ctx(GetHeapSize() / 16);
    std::uniform_int_distribution<uint8_t> actionDist{0, 2};

    for (int i = 0; i < 100; i++) {
        ctx.Allocate();
        ctx.CheckAllFills();
    }

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
    }

    while (!ctx.IsEmpty()) {
        Block block = ctx.GetRandomBlock();
        ctx.Free(block);
        ctx.CheckAllFills();
    }

    MallocStats stats;
    get_malloc_stats(&stats);
    REQUIRE(stats.totalUsed == 0);
    std::cout << "Total free after test: " << stats.totalFree << "\n";
}
