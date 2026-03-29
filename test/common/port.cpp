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
