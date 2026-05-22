#include <pulse/port.h>
#include <pulse/details/common.h>


static_assert(pulseConfig_MAX_SYSCALL_INTERRUPT_PRIORITY > 0, "Cannot be zero on ARM");


namespace {

unsigned csNesting = 0;
uint32_t csPrevState;

} // anonymous namespace


void
pulsePort_EnterCriticalSection()
{
    csPrevState = pulsePort_GetAndDisableInterrupts();
    csNesting++;
}

void
pulsePort_ExitCriticalSection()
{
    PULSE_ASSERT(csNesting);
    csNesting--;
    if (csNesting == 0) {
        pulsePort_SetInterrupts(csPrevState);
    }
}

void
pulsePort_Sleep()
{
    asm volatile ("dsb" ::: "memory");
    asm volatile ("wfi");
    asm volatile ("isb");
}
