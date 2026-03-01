#include <pulse_config.h>
#include <common.h>
#include <pulse/malloc.h>
#include <catch2/catch_test_macros.hpp>


namespace {

uint8_t heap[1024 * 1024];

bool isHeapLocked = false;

}

void
InitHeap()
{
    pulse_add_heap_region(heap, sizeof(heap));
}

void
TestMallocLock()
{
    if (isHeapLocked) {
        throw std::runtime_error("Heap already locked");
    }
    isHeapLocked = true;
}

void
TestMallocUnlock()
{
    if (!isHeapLocked) {
        throw std::runtime_error("Heap not locked");
    }
    isHeapLocked = false;
}

bool
IsHeapLocked()
{
    return isHeapLocked;
}

void
CheckInHeap(void *addr, std::size_t size)
{
    REQUIRE(reinterpret_cast<uint8_t *>(addr) >= heap);
    REQUIRE(reinterpret_cast<uint8_t *>(addr) + size <= heap + sizeof(heap));
}

std::size_t
GetHeapSize()
{
    return sizeof(heap);
}
