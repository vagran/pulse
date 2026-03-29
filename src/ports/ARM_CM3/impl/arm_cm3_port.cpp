#include <pulse/port.h>
#include <pulse/details/common.h>
#include <cmsis_gcc.h>


namespace {

unsigned csNesting = 0;

} // anonymous namespace


void
pulsePort_EnterCriticalSection()
{
    pulsePort_DisableInterrupts();
    csNesting++;
}

void
pulsePort_ExitCriticalSection()
{
    PULSE_ASSERT(csNesting);
    csNesting--;
    if (csNesting == 0) {
        pulsePort_EnableInterrupts();
    }
}

void
pulsePort_Sleep()
{
    asm volatile ("dsb" ::: "memory");
    asm volatile ("wfi");
    asm volatile ("isb");
}
