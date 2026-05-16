#ifndef PULSE_CONFIG_H
#define PULSE_CONFIG_H

#include <panic.h>
#include <pulse/defs.h>
#include <stddef.h>


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

#define pulseConfig_TICK_FREQ                       1000

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

#define pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY  0x8F

#define pulseConfig_MALLOC_FAILED_PANIC             1

#define pulseConfig_MALLOC_LOCK()                   MallocLock()
#define pulseConfig_MALLOC_UNLOCK()                 MallocUnlock()

#define pulseConfig_MALLOC_STATS                    1

#define pulseConfig_LOG_PUT_CHAR(c)                 LogPutc(c)

#define pulseConfig_LOG_GET_TIMESTAMP               LogGetTimestamp

#endif /* PULSE_CONFIG_H */
