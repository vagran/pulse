#include <pulse/port.h>
#include <catch2/catch_test_macros.hpp>
#include <interrupts.h>
#include <mutex>
#include <atomic>


namespace {

std::mutex csMutex;
bool irqEnabled = true;
thread_local bool isIsr = false;
thread_local int csNesting = 0;

} // anonymous namespace

void
IsrEnter()
{
    csMutex.lock();
    if (isIsr) {
        throw std::runtime_error("isIsr set in IsrEnter");
    }
    if (csNesting) {
        throw std::runtime_error("csNesting != 0 in IsrEnter");
    }
    isIsr = true;
}

void
IsrExit()
{
    if (!isIsr) {
        throw std::runtime_error("isIsr is not set in IsrExit");
    }
    if (csNesting) {
        throw std::runtime_error("csNesting != 0 in IsrExit");
    }
    isIsr = false;
    csMutex.unlock();
}

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
    if (csNesting == 0 && !isIsr) {
        csMutex.lock();
    }
    csNesting++;
}

void
pulsePort_ExitCriticalSection()
{
    if (csNesting == 0) {
        throw std::runtime_error("csNesting underflow");
    }
    csNesting--;
    if (csNesting == 0 && !isIsr) {
        csMutex.unlock();
    }
}

void
pulsePort_Sleep()
{}

void
pulsePort_InitScheduler()
{}
