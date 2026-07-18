#include <pulse/port.h>
#include <pulse/debug.h>


static_assert(pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY > 0, "Cannot be zero on ARM");


namespace {

unsigned csNesting = 0;
uint32_t csPrevState;

} // anonymous namespace


PULSE_WEAK void
pulsePort_EnterCriticalSection()
{
    if (csNesting == 0) {
        // Interrupt may happen before this line, however, if it uses CS, it will enter and exit it
        // before return.
        csPrevState = pulsePort_GetAndDisableInterrupts();
    }
    csNesting++;
}

PULSE_WEAK void
pulsePort_ExitCriticalSection()
{
    PULSE_ASSERT(csNesting);
    csNesting--;
    if (csNesting == 0) {
        pulsePort_SetInterrupts(csPrevState);
    }
}

PULSE_WEAK void
pulsePort_Sleep()
{
    asm volatile ("dsb" ::: "memory");
    asm volatile ("wfi");
    asm volatile ("isb");
}

PULSE_WEAK void
pulsePort_InitScheduler()
{}
