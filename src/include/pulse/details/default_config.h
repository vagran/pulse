#ifndef DEFAULT_CONFIG_H
#define DEFAULT_CONFIG_H

#include <pulse_config.h>
#include <pulse/defs.h>


/** pulseConfig_ENABLE_TIMER
 * Enable timer functionality.
 */
#ifndef pulseConfig_ENABLE_TIMER
#   define pulseConfig_ENABLE_TIMER 1
#endif

/*
 * pulseConfig_TICK_FREQ
 * pulseConfig_TICK_FREQ must be defined in order to use timer API. Should be equal to system tick
 * frequency.
 */

 /** pulseConfig_MAX_TIMERS
  * Maximal number of simultaneously scheduled timers. This includes active Timer::Delay() and
  * Timer::WaitUntil() calls.
  */
 #ifndef pulseConfig_MAX_TIMERS
#   define pulseConfig_MAX_TIMERS 16
 #endif


/* pulseConfig_MALLOC_GRANULARITY
 * Allocation block size is always multiple of this number of bytes. Must be power of two. This also
 * is alignment value for all allocated blocks.
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


/** pulseConfig_MALLOC_REGION_STRICT_CHECK
 * Enables strict checking of size and alignment of regions passed to `pulse_add_heap_region()`.
 * `pulseConfig_PANIC()` is called if check is not passed. This prevents memory wasting which may be
 * important on very tiny MCUs.
 */
#ifndef pulseConfig_MALLOC_REGION_STRICT_CHECK
#   define pulseConfig_MALLOC_REGION_STRICT_CHECK 1
#endif

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


/** pulseConfig_MALLOC_FAILED_PANIC
 * Allow panic if pulse_malloc() or pulse_realloc() fails to allocate memory.
 */
#ifndef pulseConfig_MALLOC_FAILED_PANIC
#   define pulseConfig_MALLOC_FAILED_PANIC 0
#endif


/* pulseConfig_ASSERT
 * Takes condition to check. May apply some action if condition fails.
 */
#ifndef pulseConfig_ASSERT
#   define pulseConfig_ASSERT(x)
#endif


/* pulseConfig_PANIC
 * Invoked when fatal error condition is encountered. Takes string message argument. Should not
 * return.
 */
#ifndef pulseConfig_PANIC
#   define pulseConfig_PANIC(msg) for(;;)
#endif


/* Define C++ new/delete operators bound to Pulse memory allocator. May be useful to disable this,
 * for example, in unit tests.
 */
#ifndef pulseConfig_DEFINE_CPP_NEW
#   define pulseConfig_DEFINE_CPP_NEW 1
#endif


/** pulseConfig_NUM_TASK_PRIORITIES
 * Number of priority values for scheduled tasks. Should not be greater than needed for a particular
 * application to save resources.
 */
#ifndef pulseConfig_NUM_TASK_PRIORITIES
#   define pulseConfig_NUM_TASK_PRIORITIES 4
#endif


/** pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY
 * Highest interrupt priority at which system calls can be made (those which are allowed to be
 * called from ISR). Pulse functions should never be called from ISR running on higher priorities.
 */


/** pulseConfig_FORMAT_ERROR
 * Invoked with string message argument in case of any error when formatting strings by `pulse::fmt`
 * API.
 */


/** pulseConfig_LOG_LEVEL
 * Threshold log level for `LOG_*` macros, one of `PULSE_LOG_LEVEL_*` macro.
 */
#ifndef pulseConfig_LOG_LEVEL
#   define pulseConfig_LOG_LEVEL    PULSE_LOG_LEVEL_INFO
#endif


/** pulseConfig_LOG_PUT_CHAR
 * Macro to use for outputting next character of log message. Logs are disabled if not defined.
 */


#endif /* DEFAULT_CONFIG_H */
