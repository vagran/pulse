#ifndef MALLOC_H
#define MALLOC_H

#include <pulse/details/default_config.h>
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif

void *
pulse_malloc(size_t size);

void
pulse_free(void *ptr);

void *
pulse_realloc(void *ptr, size_t newSize);


/** Register next region to run heap on. At least one region should be added to succeed next
 * allocation. Additional regions can be added at any time. Region is trimmed if necessary to ensure
 * proper alignment.
 */
void
pulse_add_heap_region(void *region, size_t size);

/** @return Maximal possible allocation size according to current heap configuration.
 */
size_t
get_malloc_max_size();


#if pulseConfig_MALLOC_STATS

typedef struct {
    size_t totalFree, totalUsed;
} MallocStats;

/** Get current allocation statistics. It might be not exactly corresponding to requested sizes
 * since internally it is counted in allocation units, and also may utilize paddings in block tail.
 */
void
get_malloc_stats(MallocStats *stats);

#endif // pulseConfig_MALLOC_STATS

#ifdef __cplusplus
}
#endif

#endif /* MALLOC_H */
