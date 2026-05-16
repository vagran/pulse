#ifndef PULSE_PORT_H
#define PULSE_PORT_H

#include <pulse/config.h>
#include <stdint.h>


#ifndef pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY
#   error pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY must be set
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
        : "=r" (prevBASEPRI)
        : "r" (newBASEPRI)
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


#define pulsePort_DisableInterrupts             pulsePort_RaiseBASEPRI
#define pulsePort_EnableInterrupts()            pulsePort_SetBASEPRI(0)
#define pulsePort_GetAndDisableInterrupts()     pulsePort_GetAndRaiseBASEPRI()
#define pulsePort_SetInterrupts(state)          pulsePort_SetBASEPRI(state)

#endif /* PULSE_PORT_H */
