#ifndef PULSE_CONFIG_H
#define PULSE_CONFIG_H

#include <pulse/defs.h>

#define pulseConfig_MALLOC_GRANULARITY              M_GRAN
#define pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE     M_BSZ
#define pulseConfig_MALLOC_ALIGNMENT                M_ALIGN

#define pulseConfig_HEAP_SIZE                       (1024 * 1024)


extern "C" void Panic(const char *msg);

#define pulseConfig_ASSERT(x) do { \
    if (!(x)) { \
        Panic("pulseConfig_ASSERT failed: " PULSE_STR(x)); \
    } \
} while (false)

#define pulseConfig_PANIC(msg) Panic(msg)

#endif /* PULSE_CONFIG_H */
