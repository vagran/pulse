#ifndef MALLOC_H
#define MALLOC_H

#include <pulse/config.h>
#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

/** Initial allocation unit in the heap will have unused padding of this size.  */
#define PULSE_MALLOC_UNIT_PADDING_SIZE \
    ((pulseConfig_MALLOC_GRANULARITY > pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE * 2) ? \
        pulseConfig_MALLOC_GRANULARITY - pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE * 2 : 0)

#ifdef __cplusplus

/** Helper stucture for allocation units. Provided heap regions should be multiple of these units.
 * Squeezing every RAM byte:
 * @code
 * MallocUnit heap[HEAP_SIZE / pulseConfig_MALLOC_GRANULARITY];
 * struct MyData {
 *     uint16_t i;
 *     uint8_t b1, b2;
 * };
 * static_assert(sizeof(MyData) <= PULSE_MALLOC_UNIT_PADDING_SIZE);
 * MyData &myData = *reinterpret_cast<MyData *>(heap[0].padding);
 * ...
 * pulse_add_heap_region(heap, sizeof(heap));
 * new(myData) MyData();
 * DoSomeStuff(myData->i);
 * @endcode
 */
struct alignas(pulseConfig_MALLOC_GRANULARITY) MallocUnit {
    union {
        uint8_t unit[pulseConfig_MALLOC_GRANULARITY];
        /** This space in the first unit can be utilized after passing the region to
            * `pulse_add_heap_region()`. Use it if each RAM byte matters on your platform.
            */
        uint8_t padding[PULSE_MALLOC_UNIT_PADDING_SIZE];
    };
};

static_assert(sizeof(MallocUnit) == pulseConfig_MALLOC_GRANULARITY);

#endif // __cplusplus


void *
pulse_malloc(size_t size);

void
pulse_free(void *ptr);

void *
pulse_realloc(void *ptr, size_t newSize);


/** Register next region to run heap on. At least one region should be added to succeed next
 * allocation. Additional regions can be added at any time. Should be properly sized and aligned if
 * `pulseConfig_MALLOC_REGION_STRICT_CHECK` is enabled (to `pulseConfig_MALLOC_GRANULARITY`).
 * Otherwise the region is trimmed if necessary to ensure proper alignment.
 */
void
pulse_add_heap_region(void *region, size_t size);

/** De-initialize heap so regions can be added again. Mostly for unit tests. */
void
pulse_reset_heap();

/** @return Maximal possible allocation size according to current heap configuration.
 */
size_t
get_malloc_max_size();


#if pulseConfig_MALLOC_STATS

typedef struct {
    size_t totalFree, totalUsed, minFree;
} MallocStats;

/** Get current allocation statistics. It might be not exactly corresponding to requested sizes
 * since internally it is counted in allocation units, and also may utilize paddings in block tail.
 */
void
get_malloc_stats(MallocStats *stats);

#endif // pulseConfig_MALLOC_STATS

#if pulseConfig_MALLOC_DEBUG

/** Perfroms heap validation as much as possible. Mostly useful with pulseConfig_ASSERT specified.
 * @return true if heap state valid.
 */
bool
validate_heap();

#endif // pulseConfig_MALLOC_DEBUG

#ifdef __cplusplus
} // extern "C"

namespace pulse {

inline void *
Malloc(size_t size)
{
    return pulse_malloc(size);
}

inline void
Free(void *ptr)
{
    return pulse_free(ptr);
}

inline void *
Realloc(void *ptr, size_t newSize)
{
    return pulse_realloc(ptr, newSize);
}

inline void
AddHeapRegion(void *region, size_t size)
{
    pulse_add_heap_region(region, size);
}

} // namespace pulse

#endif // __cplusplus

#endif /* MALLOC_H */
