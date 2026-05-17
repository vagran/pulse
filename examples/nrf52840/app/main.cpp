#include <pulse/port.h>
#include <pulse/timer.h>
#include <pulse/malloc.h>

#include <nrf52840.h>
#include <nrfx.h>
#include <nrf_gpio.h>
#include <nrfx_clock.h>
#include <nrfx_rtc.h>


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
    return 0;
    // uint32_t tick = Timer::GetTime();
    // ldiv_t r = ldiv(tick, pulseConfig_TICK_FREQ);
    // pulse::fmt::BufferOutputStream stream(buffer, bufferSize);
    // return pulse::fmt::FormatTo(stream, "{:7}.{:03}", r.quot, r.rem);
}

#define LED_PORT 0
#define LED_PIN 15 // P0.15 = LED on Nice!Nano

namespace {

MallocUnit heap[PULSE_HEAP_UNITS_SIZE_KB(10)];

struct GpioLine {
    uint32_t pin;
};

#define DEF_IO(port, pin) NRF_GPIO_PIN_MAP(port, pin)

constexpr GpioLine
    ioLed       DEF_IO(0, 15);
    // ioButton    DEF_IO(0, 17),
    // ioRotEncA   DEF_IO(0, 6),
    // ioRotEncB   DEF_IO(0, 8);


void
RtcInterruptHandler(nrfx_rtc_int_type_t intType)
{
    if (intType == nrfx_rtc_int_type_t::NRFX_RTC_INT_TICK) {
        pulse::Timer::Tick();
    }
}

void
ClockEventsHandler(nrfx_clock_evt_type_t)
{}

nrfx_rtc_t rtc0 = NRFX_RTC_INSTANCE(0);

void
InitTicks()
{
    if (nrfx_clock_init(ClockEventsHandler) != NRFX_SUCCESS) {
        Panic("Clock init failed");
    }
    nrfx_clock_enable();
    nrfx_clock_lfclk_start();

    nrfx_rtc_config_t config = {
        .prescaler = RTC_FREQ_TO_PRESCALER(pulseConfig_TICK_FREQ),
        .interrupt_priority = NRFX_RTC_DEFAULT_CONFIG_IRQ_PRIORITY,
        .tick_latency       = 1,
        .reliable           = NRFX_RTC_DEFAULT_CONFIG_RELIABLE
    };
    if (nrfx_rtc_init(&rtc0, &config, RtcInterruptHandler) != NRFX_SUCCESS) {
        Panic("RTC init failed");
    }

    nrfx_rtc_tick_enable(&rtc0, true);
    nrfx_rtc_enable(&rtc0);

    NVIC_SetPriority(RTC0_IRQn, 8);
    NVIC_EnableIRQ(RTC0_IRQn);
}

// void
// LedOn()
// {
//     nrf_gpio_pin_set(ioLed.pin);
// }

// void
// LedOff()
// {
//     nrf_gpio_pin_clear(ioLed.pin);
// }

void
LedToggle()
{
    nrf_gpio_pin_toggle(ioLed.pin);
}

TaskV
BlinkTask()
{
    while (true) {
        LedToggle();
        co_await Timer::Delay(etl::chrono::milliseconds(500));
    }
}

} // anonymous namespace


extern "C" [[noreturn]] int
main()
{
    pulse_add_heap_region(heap, sizeof(heap));

    nrf_gpio_cfg(
        NRF_GPIO_PIN_MAP(LED_PORT, LED_PIN),
        NRF_GPIO_PIN_DIR_OUTPUT,
        NRF_GPIO_PIN_INPUT_DISCONNECT,
        NRF_GPIO_PIN_NOPULL,
        NRF_GPIO_PIN_D0H1,
        NRF_GPIO_PIN_NOSENSE);

    InitTicks();

    Task::Spawn(BlinkTask()).Pin();

    Task::RunScheduler();

    Panic("Scheduler exited");
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
