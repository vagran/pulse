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
    uint32_t newBASEPRI;

    asm volatile (
        "mov %0, %1        \n"
        "msr basepri, %0   \n"
        "isb               \n"
        "dsb               \n"
        : "=r" (newBASEPRI)
        : "i" (pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY)
        : "memory"
    );
}

static inline uint32_t
pulsePort_GetAndRaiseBASEPRI()
{
    uint32_t prevBASEPRI, newBASEPRI;

    asm volatile (
        "mrs %0, basepri   \n" \
        "mov %1, %2        \n" \
        "msr basepri, %1   \n" \
        "isb               \n" \
        "dsb               \n" \
        : "=r" (prevBASEPRI), "=r" (newBASEPRI)
        : "i" (pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY)
        : "memory"
    );

    return prevBASEPRI;
}

static inline void
pulsePort_SetBASEPRI(uint32_t newMaskValue)
{
    asm volatile (
        "msr basepri, %0 "
        ::"r" (newMaskValue) : "memory"
    );
}


#define pulsePort_DisableInterrupts     pulsePort_RaiseBASEPRI
#define pulsePort_EnableInterrupts()    pulsePort_SetBASEPRI(0)

#endif /* PULSE_PORT_H */
