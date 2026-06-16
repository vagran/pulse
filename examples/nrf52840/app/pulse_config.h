#ifndef PULSE_CONFIG_H
#define PULSE_CONFIG_H

#include <panic.h>
#include <pulse/defs.h>
#include <stddef.h>
#include <nrf52840.h>


#ifdef __cplusplus
extern "C" {
#endif

void
MallocLock();

void
MallocUnlock();

void
LogPutc(char c);

size_t
LogGetTimestamp(char *buffer, size_t bufferSize);

#ifdef __cplusplus
}
#endif

#define pulseConfig_TICK_FREQ                       1024

#define pulseConfig_TICKLESS_IDLE                   1
#define pulseConfig_TICKLESS_MIN_TICKS              4

#define pulseConfig_MALLOC_GRANULARITY              8
#define pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE     2

#ifdef DEBUG
#   define pulseConfig_ASSERT(x) do { \
        if (!(x)) { \
            Panic("pulseConfig_ASSERT failed: " PULSE_STR(x)); \
        } \
    } while (false)
#else
#   define pulseConfig_ASSERT(x)
#endif

#define pulseConfig_PANIC(msg)                      Panic(msg)

#define pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY  (4 << (8U - __NVIC_PRIO_BITS))

#define pulseConfig_MALLOC_FAILED_PANIC             1

#define pulseConfig_MALLOC_LOCK()                   MallocLock()
#define pulseConfig_MALLOC_UNLOCK()                 MallocUnlock()

#define pulseConfig_MALLOC_STATS                    1

#define pulseConfig_LOG_PUT_CHAR(c)                 LogPutc(c)

#define pulseConfig_LOG_GET_TIMESTAMP               LogGetTimestamp

#define pulseConfig_PULSE_LOG_LEVEL                 PULSE_LOG_LEVEL_INFO

#define pulseConfig_SCHEDULER_STATS                 1

#endif /* PULSE_CONFIG_H */
