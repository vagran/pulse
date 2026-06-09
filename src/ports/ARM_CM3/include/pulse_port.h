#ifndef PULSE_PORT_H
#define PULSE_PORT_H

#include <pulse/config.h>
#include <stdint.h>


#ifndef pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY
#   error pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY must be set
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline void
pulsePort_RaiseBASEPRI()
{
    uint32_t newBASEPRI = pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY;

    asm volatile (
        "msr basepri, %0   \n"
        "dsb               \n"
        "isb               \n"
        :
        : "r" (newBASEPRI)
        : "memory"
    );
}

static inline uint32_t
pulsePort_GetAndRaiseBASEPRI()
{
    uint32_t prevBASEPRI, newBASEPRI = pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY;

    asm volatile (
        "mrs %0, basepri   \n" \
        "msr basepri, %1   \n" \
        "dsb               \n" \
        "isb               \n" \
        : "=&r" (prevBASEPRI)
        : "r" (newBASEPRI)
        : "memory"
    );

    return prevBASEPRI;
}

static inline uint32_t
pulsePort_GetAndSetBASEPRI(uint32_t newMaskValue)
{
    uint32_t prevBASEPRI;

    asm volatile (
        "mrs %0, basepri        \n" \
        "msr basepri, %1        \n" \
        "dsb                    \n" \
        "isb                    \n" \
        : "=&r" (prevBASEPRI)
        : "r" (newMaskValue)
        : "memory"
    );

    return prevBASEPRI;
}

static inline void
pulsePort_SetBASEPRI(uint32_t newMaskValue)
{
    asm volatile (
        "msr basepri, %0    \n"
        "dsb                \n"
        "isb                \n"
        ::"r" (newMaskValue) : "memory"
    );
}

static inline void
pulsePort_DisableIrq()
{
    asm volatile ("cpsid i" : : : "memory");
}

static inline void
pulsePort_EnableIrq()
{
    asm volatile ("cpsie i" : : : "memory");
}

static inline bool
pulsePort_IsIrqEnabled()
{
    uint32_t result;
    asm volatile("MRS %0, primask" : "=r" (result) :: "memory");
    return result == 0;
}

#ifdef __cplusplus
}
#endif

#define pulsePort_DisableInterrupts             pulsePort_RaiseBASEPRI
#define pulsePort_EnableInterrupts()            pulsePort_SetBASEPRI(0)
#define pulsePort_GetAndDisableInterrupts()     pulsePort_GetAndRaiseBASEPRI()
#define pulsePort_GetAndSetInterrupts(state)    pulsePort_GetAndSetBASEPRI(state)
#define pulsePort_SetInterrupts(state)          pulsePort_SetBASEPRI(state)

#define pulsePort_SleepGuardIrq

#endif /* PULSE_PORT_H */
