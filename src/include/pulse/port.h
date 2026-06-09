#ifndef PORT_H
#define PORT_H

// This header should be provided by the port.
#include <pulse_port.h>
#include <pulse/config.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


/** pulsePort_TickCountType
 * Type used to store time duration in ticks.
 */
#ifndef pulsePort_TickCountType
#define pulsePort_TickCountType uint32_t
#endif


/** pulsePort_DisableInterrupts
 * Either macro or function to disable interrupts (only with
 * pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY and lower).
 */
#ifndef pulsePort_DisableInterrupts
void
pulsePort_DisableInterrupts();
#endif


/** pulsePort_EnableInterrupts
 * Either macro or function to enable interrupts.
 */
#ifndef pulsePort_EnableInterrupts
void
pulsePort_EnableInterrupts();
#endif


/** pulsePort_GetAndDisableInterrupts
 * Either macro or function to get current interrupts state and disable them.
 */
#ifndef pulsePort_GetAndDisableInterrupts
uint32_t
pulsePort_GetAndDisableInterrupts();
#endif



/** pulsePort_GetAndSetInterrupts
 * Either macro or function to get current interrupts state and disable them.
 */
#ifndef pulsePort_GetAndSetInterrupts
uint32_t
pulsePort_GetAndSetInterrupts(uint32_t state);
#endif


/** pulsePort_SetInterrupts
 * Either macro or function to restore interrupts state to one returned by
 * pulsePort_GetAndDisableInterrupts().
 */
#ifndef pulsePort_SetInterrupts
void
pulsePort_SetInterrupts(uint32_t state);
#endif


/** pulsePort_DisableIrq
 * Either macro or function to disable interrupts completely (all levels by global flag).
 */
#ifndef pulsePort_DisableIrq
void
pulsePort_DisableIrq();
#endif


/** pulsePort_EnableIrq
 * Either macro or function to enable all interrupts after pulsePort_DisableIrq().
 */
#ifndef pulsePort_EnableIrq
void
pulsePort_EnableIrq();
#endif


/** pulsePort_IsIrqEnabled
 * Either macro or function to check if IRQ not masked by global flag.
 */
#ifndef pulsePort_IsIrqEnabled
bool
pulsePort_IsIrqEnabled();
#endif


/** pulsePort_EnterCriticalSection
 * Either macro or function to enter scheduler critical section.
 */
#ifndef pulsePort_EnterCriticalSection
void
pulsePort_EnterCriticalSection();
#endif


/** pulsePort_ExitCriticalSection
 * Either macro or function to exit scheduler critical section.
 */
#ifndef pulsePort_ExitCriticalSection
void
pulsePort_ExitCriticalSection();
#endif


/** pulsePort_InitScheduler
 * Either macro or function called before running scheduler.
 */
#ifndef pulsePort_InitScheduler
void
pulsePort_InitScheduler();
#endif


/** pulsePort_Sleep
 * Either macro or function to enter low-power mode sleep until next interrupt.
 */
#ifndef pulsePort_Sleep
void
pulsePort_Sleep();
#endif


#if pulseConfig_TICKLESS_IDLE

/** pulsePort_Sleep
 * Either macro or function to enter low-power mode sleep until next interrupt for the specified
 * duration. It should return number of ticks actually passed. It is called with kernel level
 * interrupts disabled.
 */
#ifndef pulsePort_TicklessSleep

pulsePort_TickCountType
pulsePort_TicklessSleep(pulsePort_TickCountType duration);

#endif

#endif // pulseConfig_TICKLESS_IDLE


/* One of `pulsePort_SleepGuardIrq` or `pulsePort_SleepGuardInterrupts` should be defined to select
 * either `pulsePort_DisableIrq` or `pulsePort_DisableInterrupts` to apply when entering low power
 * state.
 */
#if !defined(pulsePort_SleepGuardIrq) && !defined(pulsePort_SleepGuardInterrupts)
#   error Either `pulsePort_SleepGuardIrq` or `pulsePort_SleepGuardInterrupts` should be defined by port
#endif

#if defined(pulsePort_SleepGuardIrq) && defined(pulsePort_SleepGuardInterrupts)
#   error Both `pulsePort_SleepGuardIrq` or `pulsePort_SleepGuardInterrupts` defined
#endif

#ifdef pulsePort_SleepGuardIrq
#define pulsePort_SleepInterruptGuard   pulse::IrqGuard
#endif

#ifdef pulsePort_SleepGuardInterrupts
#define pulsePort_SleepInterruptGuard   pulse::InterruptsGuard
#endif


#ifdef __cplusplus
} // extern "C"

namespace pulse {

// C++ wrappers for port C macros/functions

/// This disables only interrupts with pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY and lower.
inline void
DisableInterrupts()
{
    pulsePort_DisableInterrupts();
}

inline void
EnableInterrupts()
{
    pulsePort_EnableInterrupts();
}

inline uint32_t
GetAndDisableInterrupts()
{
    return pulsePort_GetAndDisableInterrupts();
}

inline uint32_t
GetAndSetInterrupts(uint32_t state)
{
    return pulsePort_GetAndSetInterrupts(state);
}

inline void
SetInterrupts(uint32_t state)
{
    pulsePort_SetInterrupts(state);
}

inline void
DisableIrq()
{
    pulsePort_DisableIrq();
}

inline void
EnableIrq()
{
    pulsePort_EnableIrq();
}

inline void
EnterCriticalSection()
{
    pulsePort_EnterCriticalSection();
}

inline void
ExitCriticalSection()
{
    pulsePort_ExitCriticalSection();
}

namespace details {

template <uint32_t TargetPriority>
struct InterruptsGuardTrait {
    static uint32_t
    GetAndDisableInterrupt()
    {
        return pulsePort_GetAndSetInterrupts(TargetPriority);
    }
};

template <>
struct InterruptsGuardTrait<pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY> {
    static uint32_t
    GetAndDisableInterrupt()
    {
        return pulsePort_GetAndDisableInterrupts();
    }
};

} // namespace details


/** Helper class to disable ISRs in this class instance scope. */
template <uint32_t TargetPriority = pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY>
class InterruptsGuard {
public:
    bool acquired = false;
    uint32_t state;

    InterruptsGuard(const InterruptsGuard &) = delete;

    InterruptsGuard(bool acquire = true)
    {
        if (acquire) {
            state = details::InterruptsGuardTrait<TargetPriority>::GetAndDisableInterrupt();
            acquired = true;
        }
    }

    InterruptsGuard(InterruptsGuard &&other):
        acquired(other.acquired),
        state(other.state)
    {
        other.acquired = false;
    }

    ~InterruptsGuard()
    {
        Exit();
    }

    void
    Exit()
    {
        if (acquired) {
            pulsePort_SetInterrupts(state);
            acquired = false;
        }
    }
};

/** Helper class to disable IRQs in this class instance scope. */
class IrqGuard {
public:
    bool acquired = false;
    bool state;

    IrqGuard(const IrqGuard &) = delete;

    IrqGuard(bool acquire = true)
    {
        if (acquire) {
            state = pulsePort_IsIrqEnabled();
            pulsePort_DisableIrq();
            acquired = true;
        }
    }

    IrqGuard(IrqGuard &&other):
        acquired(other.acquired),
        state(other.state)
    {
        other.acquired = false;
    }

    ~IrqGuard()
    {
        Exit();
    }

    void
    Exit()
    {
        if (acquired) {
            if (state) {
                pulsePort_EnableIrq();
            }
            acquired = false;
        }
    }
};

/** Helper class to work in critical section in this class instance scope. */
class CriticalSection {
public:
    bool acquired = true;

    CriticalSection(const CriticalSection &) = delete;

    CriticalSection()
    {
        pulsePort_EnterCriticalSection();
    }

    CriticalSection(CriticalSection &&other):
        acquired(other.acquired)
    {
        other.acquired = false;
    }

    ~CriticalSection()
    {
        Exit();
    }

    void
    Exit()
    {
        if (acquired) {
            pulsePort_ExitCriticalSection();
            acquired = false;
        }
    }
};

} // namespace pulse

#endif // __cplusplus

#endif /* PORT_H */
