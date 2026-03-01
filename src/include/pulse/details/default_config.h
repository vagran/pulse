#ifndef DEFAULT_CONFIG_H
#define DEFAULT_CONFIG_H

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
 * so remaining bits define maximal supported allocation size (in `pulseConfig_MALLOC_GRANULARITY`
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
 * value. Must be power of 2. Cannot be less than pointer size.
 */
#ifndef pulseConfig_MALLOC_ALIGNMENT
#   define pulseConfig_MALLOC_ALIGNMENT 4
#endif

#if pulseConfig_MALLOC_ALIGNMENT <= 0 || !PULSE_IS_POW2(pulseConfig_MALLOC_ALIGNMENT)
#   error pulseConfig_MALLOC_ALIGNMENT must be positive power of two
#endif


/* pulseConfig_MALLOC_BEST_FIT
 * When defined, smallest enough-sized free block is used to fullfil allocation request. First fit
 * used otherwise. This may require longer traversal of free blocks list.
 */


/* pulseConfig_MALLOC_LOCK
 * User provided lock for heap operations. Not required for cooperative scheduling if allocations in
 * ISR are not permitted, otherwise should disable interrupts.
 */


/* pulseConfig_MALLOC_UNLOCK
 * Heap operations unlocking complemental to `pulseConfig_MALLOC_LOCK`.
 */


/* pulseConfig_MALLOC_FREE_SPACE_POISONING
* Fill freed blocks with the specified byte value. Mostly for testing and troubleshooting.
*/


/** pulseConfig_MALLOC_STATS
 * Maintain heap usage statistics if enabled.
 */
#ifndef pulseConfig_MALLOC_STATS
#   define pulseConfig_MALLOC_STATS 0
#endif


/** pulseConfig_MALLOC_DEBUG
 * Allow internal heap validation code. Mostly for testing. Not intended for use in production code.
 */
#ifndef pulseConfig_MALLOC_DEBUG
#   define pulseConfig_MALLOC_DEBUG 0
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


#endif /* DEFAULT_CONFIG_H */
