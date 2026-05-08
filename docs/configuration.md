# Configuration


Compile-time configuration is performed by providing a `pulse_config.h` file in any include search
path. Most options have reasonable defaults, so in typical use only a small subset needs to be
specified.

The most up-to-date list of configuration parameters is maintained in the codebase in the
[`default_config.h`](../src/include/pulse/details/default_config.h) file.


<a name="pulseConfig_ENABLE_TIMER"></a>
 - [`pulseConfig_ENABLE_TIMER`](#pulseConfig_ENABLE_TIMER)

   Enable timer functionality.

   _Default value:_ `1`


<a name="pulseConfig_TICK_FREQ"></a>
 - [`pulseConfig_TICK_FREQ`](#pulseConfig_TICK_FREQ)

   `pulseConfig_TICK_FREQ` must be defined in order to use timer API. Should be equal to system tick frequency.

   _Default value:_ _None_


<a name="pulseConfig_MAX_TIMERS"></a>
 - [`pulseConfig_MAX_TIMERS`](#pulseConfig_MAX_TIMERS)

   Maximal number of simultaneously scheduled timers. This includes active `Timer::Delay()` and `Timer::WaitUntil()` calls.

   _Default value:_ `16`


<a name="pulseConfig_MALLOC_GRANULARITY"></a>
 - [`pulseConfig_MALLOC_GRANULARITY`](#pulseConfig_MALLOC_GRANULARITY)

   Allocation block size is always multiple of this number of bytes. Must be power of two. This also is alignment value for all allocated blocks.

   _Default value:_ `8`


<a name="pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE"></a>
 - [`pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE`](#pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE)

   Defines size in bytes of block metadata field which stores block size. It has one bit reserved, so remaining bits define maximal supported allocation size (in `pulseConfig_MALLOC_GRANULARITY` units). Allocation overhead is two such fields. Must be power of two.

   _Default value:_ `2`


<a name="pulseConfig_MALLOC_BEST_FIT"></a>
 - [`pulseConfig_MALLOC_BEST_FIT`](#pulseConfig_MALLOC_BEST_FIT)

   When defined, smallest enough-sized free block is used to fulfill allocation request. First fit used otherwise. This may require longer traversal of free blocks list.

   _Default value:_ _None_


<a name="pulseConfig_MALLOC_LOCK"></a>
 - [`pulseConfig_MALLOC_LOCK`](#pulseConfig_MALLOC_LOCK)

   User provided lock for heap operations. Not required for cooperative scheduling if allocations in ISR are not permitted, otherwise should disable interrupts.

   _Default value:_ _None_


<a name="pulseConfig_MALLOC_UNLOCK"></a>
 - [`pulseConfig_MALLOC_UNLOCK`](#pulseConfig_MALLOC_UNLOCK)

   Heap operations unlocking complementary to `pulseConfig_MALLOC_LOCK`.

   _Default value:_ _None_


<a name="pulseConfig_MALLOC_FREE_SPACE_POISONING"></a>
 - [`pulseConfig_MALLOC_FREE_SPACE_POISONING`](#pulseConfig_MALLOC_FREE_SPACE_POISONING)

   Fill freed blocks with the specified byte value. Mostly for testing and troubleshooting.

   _Default value:_ _None_


<a name="pulseConfig_MALLOC_REGION_STRICT_CHECK"></a>
 - [`pulseConfig_MALLOC_REGION_STRICT_CHECK`](#pulseConfig_MALLOC_REGION_STRICT_CHECK)

   Enables strict checking of size and alignment of regions passed to `pulse_add_heap_region()`. `pulseConfig_PANIC()` is called if check is not passed. This prevents memory wasting which may be important on very tiny MCUs.

   _Default value:_ `1`


<a name="pulseConfig_MALLOC_STATS"></a>
 - [`pulseConfig_MALLOC_STATS`](#pulseConfig_MALLOC_STATS)

   Maintain heap usage statistics if enabled.

   _Default value:_ `0`


<a name="pulseConfig_MALLOC_DEBUG"></a>
 - [`pulseConfig_MALLOC_DEBUG`](#pulseConfig_MALLOC_DEBUG)

   Allow internal heap validation code. Mostly for testing. Not intended for use in production code.

   _Default value:_ `0`


<a name="pulseConfig_MALLOC_FAILED_PANIC"></a>
 - [`pulseConfig_MALLOC_FAILED_PANIC`](#pulseConfig_MALLOC_FAILED_PANIC)

   Allow panic if `pulse_malloc()` or `pulse_realloc()` fails to allocate memory.

   _Default value:_ `0`


<a name="pulseConfig_ASSERT"></a>
 - [`pulseConfig_ASSERT`](#pulseConfig_ASSERT)

   Takes condition to check. May apply some action if condition fails.

   _Default value:_ _None_


<a name="pulseConfig_PANIC"></a>
 - [`pulseConfig_PANIC`](#pulseConfig_PANIC)

   Invoked when fatal error condition is encountered. Takes string message argument. Should not return.

   _Default value:_ `for(;;)`


<a name="pulseConfig_DEFINE_CPP_NEW"></a>
 - [`pulseConfig_DEFINE_CPP_NEW`](#pulseConfig_DEFINE_CPP_NEW)

   Define C++ `new`/`delete` operators bound to Pulse memory allocator. May be useful to disable this, for example, in unit tests.

   _Default value:_ `1`


<a name="pulseConfig_NUM_TASK_PRIORITIES"></a>
 - [`pulseConfig_NUM_TASK_PRIORITIES`](#pulseConfig_NUM_TASK_PRIORITIES)

   Number of priority values for scheduled tasks. Should not be greater than needed for a particular application to save resources.

   _Default value:_ `4`


<a name="pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY"></a>
 - [`pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY`](#pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY)

   Highest interrupt priority at which system calls can be made (those which are allowed to be called from ISR). Pulse functions should never be called from ISR running on higher priorities.

   _Default value:_ _None_

<a name="pulseConfig_FORMAT_ERROR"></a>
 - [`pulseConfig_FORMAT_ERROR`](#pulseConfig_FORMAT_ERROR)

   Invoked with string message argument in case of any error when formatting strings by `pulse::fmt` API.

   _Default value:_ _None_
