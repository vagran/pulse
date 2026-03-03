#ifndef COMMON_PULSE_CONFIG_H
#define COMMON_PULSE_CONFIG_H

// Validate locking.
extern "C" void TestMallocLock();
extern "C" void TestMallocUnlock();

#define pulseConfig_MALLOC_LOCK                     TestMallocLock
#define pulseConfig_MALLOC_UNLOCK                   TestMallocUnlock

extern "C" void Panic(const char *msg);

#define pulseConfig_ASSERT(x) do { \
    if (!(x)) { \
        Panic("pulseConfig_ASSERT failed: " PULSE_STR(x)); \
    } \
} while (false)

#define pulseConfig_PANIC(msg) Panic(msg)

#define pulseConfig_MALLOC_STATS                    1

#define pulseConfig_MALLOC_FREE_SPACE_POISONING     0xfe

#define pulseConfig_MALLOC_DEBUG                    1

#define pulseConfig_MALLOC_REGION_STRICT_CHECK      1

#endif /* COMMON_PULSE_CONFIG_H */
