#include <pulse/port.h>
#include <catch2/catch_test_macros.hpp>


namespace {

int csNesting = 0;
bool irqEnabled = true;

} // anonymous namespace

void
pulsePort_DisableInterrupt()
{}


void
pulsePort_EnableInterrupts()
{}

void
pulsePort_DisableIrq()
{
    irqEnabled = false;
}

void
pulsePort_EnableIrq()
{
    irqEnabled = true;
}

bool
pulsePort_IsIrqEnabled()
{
    return irqEnabled;
}

unsigned
pulsePort_GetAndDisableInterrupts()
{
    return 0;
}

void
pulsePort_SetInterrupts(unsigned)
{}

void
pulsePort_EnterCriticalSection()
{
    csNesting++;
}

void
pulsePort_ExitCriticalSection()
{
    REQUIRE(csNesting > 0);
    csNesting--;
}

void
pulsePort_Sleep()
{}

void
pulsePort_InitScheduler()
{}
