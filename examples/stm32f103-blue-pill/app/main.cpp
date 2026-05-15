#include <pulse/malloc.h>
#include <pulse/task.h>
#include <pulse/timer.h>
#include <pulse/port.h>
#include <pulse/token_queue.h>
#include <pulse/discard_queue.h>
#include <pulse/log.h>

#include <stm32f1xx_hal.h>
#include <stm32f103xb.h>

#include <uart.h>


using namespace pulse;


[[noreturn]] void
Panic(const char *msg)
{
    DisableInterrupts();
    uart.PanicFlush();

    for (const char *p = msg; *p; p++) {
        uart.WriteCharSync(*p);
    }

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

namespace {

struct GpioLine {
    uintptr_t port;
    uint16_t pin;

    GPIO_TypeDef *
    Port() const
    {
        return reinterpret_cast<GPIO_TypeDef *>(port);
    }
};

#define DEF_IO(port, pin) {GPIO ## port ## _BASE, GPIO_PIN_ ## pin}

constexpr GpioLine
    ioLed       DEF_IO(C, 13),
    ioButton    DEF_IO(C, 14),
    ioRotEncA   DEF_IO(A, 10),
    ioRotEncB   DEF_IO(A, 11);

MallocUnit heap[PULSE_HEAP_UNITS_SIZE_KB(15)];

void
SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Initializes the RCC Oscillators according to the specified parameters
     * in the RCC_OscInitTypeDef structure.
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Panic("HAL_RCC_OscConfig failed");
    }
    /** Initializes the CPU, AHB and APB buses clocks
     */
    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        Panic("HAL_RCC_ClockConfig failed");
    }
}

void
InitLed(void)
{
    GPIO_InitTypeDef init = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    // Initially off (active low)
    HAL_GPIO_WritePin(ioLed.Port(), ioLed.pin, GPIO_PIN_SET);

    init.Pin = ioLed.pin;
    init.Mode = GPIO_MODE_OUTPUT_OD;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ioLed.Port(), &init);
}

void
InitButton()
{
    GPIO_InitTypeDef init = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    init.Pin = ioButton.pin;
    init.Mode = GPIO_MODE_IT_FALLING;
    init.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(ioButton.Port(), &init);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 8, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void
InitRotaryEncoder()
{
    GPIO_InitTypeDef init = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    init.Pin = ioRotEncA.pin;
    init.Mode = GPIO_MODE_IT_FALLING;
    init.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(ioRotEncA.Port(), &init);

    init.Pin = ioRotEncB.pin;
    init.Mode = GPIO_MODE_IT_FALLING;
    init.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(ioRotEncB.Port(), &init);
}

void
LedOn()
{
    HAL_GPIO_WritePin(ioLed.Port(), ioLed.pin, GPIO_PIN_RESET);
}

void
LedOff()
{
    HAL_GPIO_WritePin(ioLed.Port(), ioLed.pin, GPIO_PIN_SET);
}

void
LedToggle()
{
    HAL_GPIO_TogglePin(ioLed.Port(), ioLed.pin);
}

bool
IsButtonPressed()
{
    // Active low.
    return HAL_GPIO_ReadPin(ioButton.Port(), ioButton.pin) == GPIO_PIN_RESET;
}

Timer blinkTimer;
int blinkIntervalIndex = 0;
TokenQueue<uint8_t> buttonEvents(5);

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
TaskV
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
TaskV
ButtonTask()
{
    constexpr auto JITTER_DELAY = etl::chrono::milliseconds(20);
    Timer jitterTimer;

    while (true) {
        co_await buttonEvents;
        // Suppress jitter - wait until active level is stable for a long period.
        bool pressed = false;
        while (true) {
            jitterTimer.ExpiresAfter(JITTER_DELAY);
            size_t idx = co_await Task::WhenAny(buttonEvents, jitterTimer);
            if (idx == 0) {
                // Button pressed again, restart anti-jitter delay
                continue;
            }
            // Anti-jitter delay expired with no new presses. Check if button was not released
            // before delay expired.
            if (!IsButtonPressed()) {
                break;
            }
            pressed = true;
            break;
        }

        if (!pressed) {
            // Ignore too short press
            continue;
        }

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
    TokenQueue<uint8_t> lineAEvent{5}, lineBEvents{5};
    etl::optional<bool> lastDir;
    bool lastLine = false, halfClick = false;
    InlineDiscardQueue<int8_t, true, 16> clicks;

    TaskV
    LineTask(bool isA);

    static bool
    GetLineState(bool isA)
    {
        GpioLine line = isA ? ioRotEncA : ioRotEncB;
        return HAL_GPIO_ReadPin(line.Port(), line.pin) == GPIO_PIN_RESET;
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
    (isA ? lineAEvent : lineBEvents).Push();
}

void
RotaryEncoder::Initialize()
{
    Task::Spawn(LineTask(true), Task::HIGHEST_PRIORITY).Pin();
    Task::Spawn(LineTask(false), Task::HIGHEST_PRIORITY).Pin();
}

TaskV
RotaryEncoder::LineTask(bool isA)
{
    constexpr auto JITTER_DELAY = etl::chrono::milliseconds(1);
    Timer jitterTimer;

    while (true) {
        co_await (isA ? lineAEvent : lineBEvents);
        // Suppress jitter - wait until active level is stable for a long period.
        bool pressed = false;
        while (true) {
            jitterTimer.ExpiresAfter(JITTER_DELAY);
            size_t idx = co_await Task::WhenAny((isA ? lineAEvent : lineBEvents), jitterTimer);
            if (idx == 0) {
                // Activated again, restart anti-jitter delay
                continue;
            }
            // Anti-jitter delay expired with no new events. Check if signal still active.
            if (!GetLineState(isA)) {
                break;
            }
            pressed = true;
            break;
        }

        if (!pressed) {
            // Ignore too short activation.
            continue;
        }
        CommitEvent(isA, GetLineState(!isA));
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

TaskV
RotaryEncoderTask()
{
    while (true) {
        int8_t clicks = co_await rotEnc.WaitClick();
        LOG_INFO("Rotary encoder: {}", clicks);
    }
}

} /* anonymous namespace */

extern "C" void
EXTI15_10_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(ioButton.pin);
    HAL_GPIO_EXTI_IRQHandler(ioRotEncA.pin);
    HAL_GPIO_EXTI_IRQHandler(ioRotEncB.pin);
}

void
HAL_GPIO_EXTI_Callback(uint16_t gpioPin)
{
    if (gpioPin == ioButton.pin) {
        buttonEvents.Push();
    } else if (gpioPin == ioRotEncA.pin) {
        rotEnc.OnLineInterrupt(true);
    } else if (gpioPin == ioRotEncB.pin) {
        rotEnc.OnLineInterrupt(false);
    }
}

void
LogPutc(char c)
{
    uart.WriteChar(c);
}

size_t
LogGetTimestamp(char *buffer, size_t bufferSize)
{
    uint32_t tick = HAL_GetTick();
    ldiv_t r = ldiv(tick, pulseConfig_TICK_FREQ);
    pulse::fmt::BufferOutputStream stream(buffer, bufferSize);
    return pulse::fmt::FormatTo(stream, "{:7}.{:03}", r.quot, r.rem);
}

extern "C" [[noreturn]] int
main()
{
    pulse_add_heap_region(heap, sizeof(heap));

    HAL_Init();
    SystemClock_Config();

    static uint8_t uartBuffer[1024];
    uart.Initialize(230400, uartBuffer, sizeof(uartBuffer));

    LOG_INFO("Application started - " PULSE_STR(BUILDER));

    InitLed();
    InitRotaryEncoder();
    InitButton();
    rotEnc.Initialize();

    Task::Spawn(BlinkTask()).Pin();
    Task::Spawn(ButtonTask()).Pin();
    Task::Spawn(RotaryEncoderTask()).Pin();

    Task::RunScheduler();

    Panic("Scheduler exited");
}


// Normally provided by libgcc
#ifdef __clang__
extern "C" void
_init()
{}

extern "C" void
_fini()
{}
#endif // __clang__
