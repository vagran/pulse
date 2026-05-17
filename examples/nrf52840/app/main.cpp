#include <pulse/port.h>
#include <nrf52840.h>
#include <nrfx.h>
#include <nrf_gpio.h>


using namespace pulse;


[[noreturn]] void
Panic(const char *msg)
{
    DisableInterrupts();
    //XXX
    // uart.PanicFlush();

    // for (const char *p = msg; *p; p++) {
    //     uart.WriteCharSync(*p);
    // }

    /* Stop in debug build, reset after delay in release build. */
#ifdef DEBUG
    for(;;);
#else
    /* Make delay before reset */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) {
        /* 1 second delay. */
        uint32_t cycles = SystemCoreClock;
        uint32_t start = DWT->CYCCNT;
        while ((DWT->CYCCNT - start) < cycles);
    }

    NVIC_SystemReset();
#endif
}

void
MallocLock()
{
    EnterCriticalSection();
}

void
MallocUnlock()
{
    ExitCriticalSection();
}

void
LogPutc(char c)
{
    //XXX
    // uart.WriteChar(c);
}

size_t
LogGetTimestamp(char *buffer, size_t bufferSize)
{
    //XXX
    return 0;
    // uint32_t tick = HAL_GetTick();
    // ldiv_t r = ldiv(tick, pulseConfig_TICK_FREQ);
    // pulse::fmt::BufferOutputStream stream(buffer, bufferSize);
    // return pulse::fmt::FormatTo(stream, "{:7}.{:03}", r.quot, r.rem);
}

#define LED_PORT 0
#define LED_PIN 15 // P0.15 = LED on Nice!Nano


extern "C" [[noreturn]] int
main()
{
    nrf_gpio_cfg(
        NRF_GPIO_PIN_MAP(LED_PORT, LED_PIN),
        NRF_GPIO_PIN_DIR_OUTPUT,
        NRF_GPIO_PIN_INPUT_DISCONNECT,
        NRF_GPIO_PIN_NOPULL,
        NRF_GPIO_PIN_D0H1,
        NRF_GPIO_PIN_NOSENSE);

    while (true) {
        NRFX_DELAY_US(500000);
        nrf_gpio_pin_toggle(NRF_GPIO_PIN_MAP(LED_PORT, LED_PIN));
    }
}

extern "C" void
_init()
{}

extern "C" void
_fini()
{}

// Prevent memory wasting for libc atexit.
extern "C" int
__wrap_atexit(void (*)())
{
    return -1;
}
