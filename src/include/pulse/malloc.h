#ifndef MALLOC_H
#define MALLOC_H

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

/** Register next region to run heap on. pulseConfig_MULTI_REGIONS should be specified in the
 * configuration.
 */
void
pulse_add_heap_region(void *region, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* MALLOC_H */
