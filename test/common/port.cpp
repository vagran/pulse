#include <pulse/port.h>
#include <catch2/catch_test_macros.hpp>


namespace {

int csNesting = 0;

} // anonymous namespace

void
pulsePort_DisableInterrupt()
{}


void
pulsePort_EnableInterrupts()
{}

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
