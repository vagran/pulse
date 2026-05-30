#include <uart.h>

#include <pulse/port.h>
#include <pulse/timer.h>
#include <pulse/malloc.h>
#include <pulse/log.h>
#include <pulse/discard_queue.h>

#include <nrf52840.h>
#include <nrfx.h>
#include <nrf_gpio.h>
#include <nrfx_gpiote.h>
#include <nrfx_clock.h>
#include <nrfx_rtc.h>
#include <nrf_power.h>


using namespace pulse;


[[noreturn]] void
Panic(const char *msg)
{
    DisableIrq();

    uart.PanicFlush();

    for (const char *p = msg; *p; p++) {
        uart.WriteCharSync(*p);
    }
    uart.WriteCharSync('\n');

    /* Stop in debug build, reset after delay in release build. */
#ifdef DEBUG
    for(;;);
#else
    /* Make delay before reset */
    NRFX_DELAY_US(500000);

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
    uart.WriteChar(c);
}

size_t
LogGetTimestamp(char *buffer, size_t bufferSize)
{
    uint32_t tick = Timer::GetTime();
    ldiv_t r = ldiv(tick, pulseConfig_TICK_FREQ);
    uint32_t ms;
    if constexpr (pulseConfig_TICK_FREQ != 1000) {
        ms = r.rem * 1000 / pulseConfig_TICK_FREQ;
    } else {
        ms = r.rem;
    }
    pulse::fmt::BufferOutputStream stream(buffer, bufferSize);
    return pulse::fmt::FormatTo(stream, "{:7}.{:03}", r.quot, ms);
}

namespace {

MallocUnit heap[PULSE_HEAP_UNITS_SIZE_KB(10)];

struct GpioLine {
    uint32_t pin;
};

#define DEF_IO(port, pin) NRF_GPIO_PIN_MAP(port, pin)

constexpr GpioLine
    ioLed       DEF_IO(0, 15),
    ioButton    DEF_IO(0, 17),
    ioRotEncA   DEF_IO(0, 6),
    ioRotEncB   DEF_IO(0, 8);

/// Configure UICR_REGOUT0 register to set GPIO output voltage to 2.7V. Setting 3.0V makes OpenOCD
/// with FT232H unable to upload firmware.
void
GpioOutputVoltageSetup(void)
{
    uint32_t targetVoltage = UICR_REGOUT0_VOUT_2V7;
    // Configure UICR_REGOUT0 register only if it is not already properly set.
    if ((NRF_UICR->REGOUT0 & UICR_REGOUT0_VOUT_Msk) !=
        (targetVoltage << UICR_REGOUT0_VOUT_Pos)) {

        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy){}

        NRF_UICR->REGOUT0 = (NRF_UICR->REGOUT0 & ~((uint32_t)UICR_REGOUT0_VOUT_Msk)) |
                            (targetVoltage << UICR_REGOUT0_VOUT_Pos);

        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy){}

        // System reset is needed to update UICR registers.
        NVIC_SystemReset();
    }
}

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
        .prescaler          = RTC_FREQ_TO_PRESCALER(pulseConfig_TICK_FREQ),
        .interrupt_priority = NRFX_RTC_DEFAULT_CONFIG_IRQ_PRIORITY,
        .tick_latency       = 1,
        .reliable           = NRFX_RTC_DEFAULT_CONFIG_RELIABLE
    };
    if (nrfx_rtc_init(&rtc0, &config, RtcInterruptHandler) != NRFX_SUCCESS) {
        Panic("RTC init failed");
    }

    nrfx_rtc_tick_enable(&rtc0, true);
    nrfx_rtc_enable(&rtc0);
}

void
InitLed()
{
    nrf_gpio_cfg(
        ioLed.pin,
        NRF_GPIO_PIN_DIR_OUTPUT,
        NRF_GPIO_PIN_INPUT_DISCONNECT,
        NRF_GPIO_PIN_NOPULL,
        NRF_GPIO_PIN_D0H1,
        NRF_GPIO_PIN_NOSENSE);
}

Timer blinkTimer;
int blinkIntervalIndex = 0;
// Published from the button ISR, `true` for pressed event, `false` for release.
InlineDiscardQueue<bool, true, 8> buttonEvents;

bool
IsButtonPressed()
{
    return !nrf_gpio_pin_read(ioButton.pin);
}

void
PinStateHandler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action);

void
InitButton()
{
    if (nrfx_gpiote_init() != NRFX_SUCCESS) {
        Panic("GPIOTE init failed");
    }

    nrfx_gpiote_in_config_t config = NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(true);
    config.pull = NRF_GPIO_PIN_PULLUP;

    if (nrfx_gpiote_in_init(ioButton.pin, &config, PinStateHandler) != NRFX_SUCCESS) {
        Panic("Pin init failed");
    }

    nrfx_gpiote_in_event_enable(ioButton.pin, true);
}

void
InitRotaryEncoder()
{
    nrfx_gpiote_in_config_t config = NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(true);
    config.pull = NRF_GPIO_PIN_PULLUP;

    if (nrfx_gpiote_in_init(ioRotEncA.pin, &config, PinStateHandler) != NRFX_SUCCESS ||
        nrfx_gpiote_in_init(ioRotEncB.pin, &config, PinStateHandler) != NRFX_SUCCESS) {

        Panic("Rotary encoder init failed");
    }

    nrfx_gpiote_in_event_enable(ioRotEncA.pin, true);
    nrfx_gpiote_in_event_enable(ioRotEncB.pin, true);
}

void
LedOn()
{
    nrf_gpio_pin_set(ioLed.pin);
}

void
LedOff()
{
    nrf_gpio_pin_clear(ioLed.pin);
}

void
LedToggle()
{
    nrf_gpio_pin_toggle(ioLed.pin);
}

Awaitable<void>
ConfirmSwitch(int intervalIndex)
{
    LedOff();
    co_await Timer::Delay(etl::chrono::milliseconds(500));
    for (int i = 0; i <= intervalIndex; i++) {
        LedOn();
        co_await Timer::Delay(etl::chrono::milliseconds(100));
        LedOff();
        co_await Timer::Delay(etl::chrono::milliseconds(200));
    }
    co_await Timer::Delay(etl::chrono::milliseconds(500));
}

// Handles LED blinking
Task<>
BlinkTask()
{
    int intervalIndex = blinkIntervalIndex;

    while (true) {
        if (intervalIndex != blinkIntervalIndex) {
            intervalIndex = blinkIntervalIndex;
            co_await ConfirmSwitch(intervalIndex);
            LedOn();
            continue;
        }
        blinkTimer.ExpiresAfter(etl::chrono::milliseconds(250 << intervalIndex));
        if (!co_await blinkTimer) {
            // Timer cancelled, so interval is definitely changed
            intervalIndex = blinkIntervalIndex;
            co_await ConfirmSwitch(intervalIndex);
            LedOn();
        } else {
            LedToggle();
        }
    }
}

// Handles button debouncing
Task<>
ButtonTask()
{
    constexpr auto JITTER_DELAY = etl::chrono::milliseconds(50);
    Timer jitterTimer;

    while (true) {
        // Wait for press
        while (!co_await buttonEvents.Pop());

        // Do action here. In case long presses supported, should differentiate from long press
        // first.
        blinkIntervalIndex++;
        if (blinkIntervalIndex > 3) {
            blinkIntervalIndex = 0;
        }
        blinkTimer.Cancel();

        LOG_INFO("New interval: {}", blinkIntervalIndex);

        MallocStats stats;
        pulse::GetMallocStats(&stats);
        uart.Format("Total free: {}\n", stats.totalFree);
        uart.Format("Total used: {}\n", stats.totalUsed);
        uart.Format("Min free: {}\n", stats.minFree);
        uart.Format("Blocks allocated: {}\n", stats.numBlocksAllocated);

        // Suppress jitter - wait until inactive level is stable for a long period
        bool isPressed = true;
        while (true) {
            jitterTimer.ExpiresAfter(JITTER_DELAY);
            size_t idx = co_await tasks::WhenAny(
                tasks::SaveResult(buttonEvents.Pop(), isPressed), jitterTimer);
            if (idx == 0) {
                // Button toggled again, restart anti-jitter delay
                continue;
            }
            // Anti-jitter delay expired with no new toggles
            break;
        }
        if (!isPressed) {
            // Release debounced
            continue;
        }

        // Potential long press, not handled currently, just debounce release
        while (true) {
            // Wait for release
            while (co_await buttonEvents.Pop());

            // Suppress release jitter
            bool isPressed = false;
            while (true) {
                jitterTimer.ExpiresAfter(JITTER_DELAY);
                size_t idx = co_await tasks::WhenAny(
                    tasks::SaveResult(buttonEvents.Pop(), isPressed), jitterTimer);
                if (idx == 0) {
                    continue;
                }
                // Anti-jitter delay expired with no new toggles
                break;
            }
            if (!isPressed) {
                // Release debounced
                break;
            }
            // Long press continues
        }
    }
}

class RotaryEncoder {
public:
    void
    Initialize();

    void
    OnLineInterrupt(bool isA);

    /** Get next click event. Value is number of clicked accumulated in given direction. Direction
     * represented by sign.
     */
    Awaitable<int8_t>
    WaitClick()
    {
        co_return co_await clicks.Pop();
    }

private:
    InlineDiscardQueue<bool, true, 8> lineAEvents, lineBEvents;
    etl::optional<bool> lastDir;
    bool lastLine = false, halfClick = false;
    InlineDiscardQueue<int8_t, true, 16> clicks;

    Task<>
    LineTask(bool isA);

    static bool
    GetLineState(bool isA)
    {
        GpioLine line = isA ? ioRotEncA : ioRotEncB;
        return !nrf_gpio_pin_read(line.pin);
    }

    void
    CommitEvent(bool triggerLineA, bool adjLineState);

    void
    CommitClick(bool dir);
};

RotaryEncoder rotEnc;

void
RotaryEncoder::OnLineInterrupt(bool isA)
{
    (isA ? lineAEvents : lineBEvents).Push(GetLineState(isA));
}

void
RotaryEncoder::Initialize()
{
    tasks::Spawn(LineTask(true), tasks::HIGHEST_PRIORITY).Pin();
    tasks::Spawn(LineTask(false), tasks::HIGHEST_PRIORITY).Pin();
}

Task<>
RotaryEncoder::LineTask(bool isA)
{
    constexpr auto JITTER_DELAY = etl::chrono::milliseconds(1);
    Timer jitterTimer;

    auto &lineEvents = isA ? lineAEvents : lineBEvents;

    while (true) {
        // Wait for press
        while (!co_await lineEvents.Pop());

        CommitEvent(isA, GetLineState(!isA));

        // Suppress jitter - wait until inactive level is stable for a long period
        bool isPressed = true;
        while (true) {
            jitterTimer.ExpiresAfter(JITTER_DELAY);
            size_t idx = co_await tasks::WhenAny(
                tasks::SaveResult(lineEvents, isPressed), jitterTimer);
            if (idx == 0) {
                // Button toggled again, restart anti-jitter delay
                continue;
            }
            // Anti-jitter delay expired with no new toggles
            break;
        }
        if (!isPressed) {
            // Release debounced
            continue;
        }

        // Still active, debounce release
        while (true) {
            // Wait for release
            while (co_await lineEvents.Pop());

            // Suppress release jitter
            bool isPressed = false;
            while (true) {
                jitterTimer.ExpiresAfter(JITTER_DELAY);
                size_t idx = co_await tasks::WhenAny(
                    tasks::SaveResult(lineEvents, isPressed), jitterTimer);
                if (idx == 0) {
                    continue;
                }
                // Anti-jitter delay expired with no new toggles
                break;
            }
            if (!isPressed) {
                // Release debounced
                break;
            }
            // Still active
        }
    }
}

void
RotaryEncoder::CommitEvent(bool triggerLineA, bool adjLineState)
{
    bool dir = triggerLineA == adjLineState;
    if (!lastDir || *lastDir != dir) {
        lastDir = dir;
        lastLine = triggerLineA;
        halfClick = true;
        return;
    }
    if (lastLine == triggerLineA) {
        return;
    }
    lastLine = triggerLineA;
    if (halfClick) {
        CommitClick(dir);
        halfClick = false;
    } else {
        halfClick = true;
    }
}

void
RotaryEncoder::CommitClick(bool dir)
{
    if (clicks.IsEmpty()) {
        clicks.Push(dir ? 1 : -1);
        return;
    }
    bool lastDir = clicks.PeekLast() > 0;
    if (dir == lastDir) {
        clicks.PeekLast() += dir ? 1 : -1;
    } else {
        clicks.Push(dir ? 1 : -1);
    }
}

Task<>
RotaryEncoderTask()
{
    while (true) {
        int8_t clicks = co_await rotEnc.WaitClick();
        LOG_INFO("Rotary encoder: {}", clicks);
    }
}

void
PinStateHandler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
    if (pin == ioButton.pin) {
        // May produce wrong value on fast bouncing. However last release will always be detected as
        // release.
        buttonEvents.Push(IsButtonPressed());
    } else if (pin == ioRotEncA.pin) {
        rotEnc.OnLineInterrupt(true);
    } else if (pin == ioRotEncB.pin) {
        rotEnc.OnLineInterrupt(false);
    }
}

} // anonymous namespace


extern "C" [[noreturn]] int
main()
{
    GpioOutputVoltageSetup();

    pulse_add_heap_region(heap, sizeof(heap));

    static uint8_t uartBuffer[1024];
    uart.Initialize(NRF_UART_BAUDRATE_230400, uartBuffer, sizeof(uartBuffer));

    LOG_INFO("Application started - " PULSE_STR(BUILDER));

    InitTicks();

    InitLed();
    InitButton();
    InitRotaryEncoder();
    rotEnc.Initialize();

    tasks::Spawn(BlinkTask()).Pin();
    tasks::Spawn(ButtonTask()).Pin();
    tasks::Spawn(RotaryEncoderTask()).Pin();

    tasks::RunScheduler();

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

uint32_t
pulsePort_TicklessSleep(uint32_t duration)
{
    static constexpr uint32_t RTC_COUNTER_BITS = 24;
    static constexpr uint32_t RTC_COUNTER_MASK = (1u << RTC_COUNTER_BITS) - 1;

    if (duration == 0) {
        return 0;
    }

    nrfx_rtc_tick_disable(&rtc0);

    uint32_t start = nrfx_rtc_counter_get(&rtc0);

    uint32_t target = (start + duration) & RTC_COUNTER_MASK;

    nrfx_rtc_cc_set(&rtc0, 0, target, true);

    pulsePort_Sleep();

    uint32_t end = nrfx_rtc_counter_get(&rtc0);

    nrfx_rtc_cc_disable(&rtc0, 0);
    nrfx_rtc_tick_enable(&rtc0, true);

    uint32_t elapsed = (end - start) & RTC_COUNTER_MASK;

    if (elapsed > duration) {
        elapsed = duration;
    }

    return elapsed;
}
