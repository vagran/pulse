#ifndef COMMON_PULSE_CONFIG_H
#define COMMON_PULSE_CONFIG_H

#define pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY  4

#define pulseConfig_TICK_FREQ 100

// Validate locking.
extern "C" void TestMallocLock();
extern "C" void TestMallocUnlock();

#define pulseConfig_MALLOC_LOCK                     TestMallocLock
#define pulseConfig_MALLOC_UNLOCK                   TestMallocUnlock

extern "C" void Panic(const char *msg);

#define pulseConfig_ASSERT(x) do { \
    if (!(x)) PULSE_UNLIKELY { \
        Panic("pulseConfig_ASSERT failed [" __FILE__ ":" PULSE_STR(__LINE__) "]: " PULSE_STR(x)); \
    } \
} while (false)

#define pulseConfig_PANIC(msg)                      Panic(msg)

#define pulseConfig_MALLOC_STATS                    1

#define pulseConfig_MALLOC_FREE_SPACE_POISONING     0xfe

#define pulseConfig_MALLOC_DEBUG                    1

#define pulseConfig_MALLOC_REGION_STRICT_CHECK      1

#define pulseConfig_DEFINE_CPP_NEW                  0

extern "C" void LogPutChar(char c);

#define pulseConfig_LOG_PUT_CHAR                    LogPutChar
#define pulseConfig_LOG_LEVEL                       PULSE_LOG_LEVEL_INFO
#define pulseConfig_PULSE_LOG_LEVEL                 PULSE_LOG_LEVEL_INFO

#endif /* COMMON_PULSE_CONFIG_H */
