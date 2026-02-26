#ifndef PULSE_DEFAULT_CONFIG_H
#define PULSE_DEFAULT_CONFIG_H

#include <pulse_config.h>
#include <pulse/defs.h>


/* pulseConfig_MALLOC_GRANULARITY
 * Allocation block size is always multiple of this number of bytes. Must be power of two.
 */
#ifndef pulseConfig_MALLOC_GRANULARITY
#   define pulseConfig_MALLOC_GRANULARITY 8
#endif

#if pulseConfig_MALLOC_GRANULARITY <= 0 || !PULSE_IS_POW2(pulseConfig_MALLOC_GRANULARITY)
#   error pulseConfig_MALLOC_GRANULARITY must be positive power of two
#endif


/* pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE
 * Defines size in bytes of block metadata field which stores block size. It has one bit reserved,
 * so remaining bits define maximal supported allocation size (in pulseConfig_MALLOC_GRANULARITY
 * units). Allocation overhead is two such fields. Must be power of two.
 */
#ifndef pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE
#   define pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE 2
#endif

#if pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE <= 0 || !PULSE_IS_POW2(pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE)
#   error pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE must be positive power of two
#endif


/* pulseConfig_MALLOC_ALIGNMENT
 * Default alignment of allocated blocks. Allocated blocks are always aligned at least by this
 * value (even if smaller alignment is requested by pulse_aligned_alloc()).
 */
#ifndef pulseConfig_MALLOC_ALIGNMENT
#   define pulseConfig_MALLOC_ALIGNMENT 4
#endif

#if pulseConfig_MALLOC_ALIGNMENT <= 0 || !PULSE_IS_POW2(pulseConfig_MALLOC_ALIGNMENT)
#   error pulseConfig_MALLOC_ALIGNMENT must be positive power of two
#endif


/* pulseConfig_MALLOC_LOCK
 * User provided lock for heap operations. Not required for cooperative scheduling if allocations in
 * ISR are not permitted, otherwise should disable interrupts.
 */
#ifndef pulseConfig_MALLOC_LOCK
#define pulseConfig_MALLOC_LOCK()
#endif


/* pulseConfig_MALLOC_UNLOCK
 * Heap operations unlocking complemental to pulseConfig_MALLOC_LOCK.
 */
#ifndef pulseConfig_MALLOC_UNLOCK
#define pulseConfig_MALLOC_UNLOCK()
#endif


/* pulseConfig_HEAP_SIZE
 * Heap size if using default single region (allocated in BSS) or single explicitly provided region
 * via pulseConfig_PROVIDED_HEAP.
 */


/* pulseConfig_PROVIDED_HEAP
 * Byte array to use as single heap region. Size should be specified by pulseConfig_HEAP_SIZE.
 */


/* pulseConfig_HEAP_MULTI_REGIONS
 * Enable heap running on multiple regions. Value should be maximal number of regions.
 * pulse_add_heap_region() must be called for each provided region before any allocation.
 */
#ifndef pulseConfig_HEAP_MULTI_REGIONS
#define pulseConfig_HEAP_MULTI_REGIONS 0
#endif


/* pulseConfig_HEAP_STRICT_REGION
 * Do not allow improperly aligned or sized provided heap regions, panic immediately if such
 * condition is detected. Silently align/trim provided region if disabled.
 */
#ifndef pulseConfig_HEAP_STRICT_REGION
#define pulseConfig_HEAP_STRICT_REGION 1
#endif


/* pulseConfig_ASSERT
 * Takes condition to check. May apply some action if condition fails.
 */
#ifndef pulseConfig_ASSERT
#define pulseConfig_ASSERT(x)
#endif


/* pulseConfig_PANIC
 * Invoked when fatal error condition is encountered. Takes string message argument. Should not
 * return.
 */
#ifndef pulseConfig_PANIC
#define pulseConfig_PANIC(msg) for(;;)
#endif


#endif /* PULSE_DEFAULT_CONFIG_H */
