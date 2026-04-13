#include <pulse/malloc.h>
#include <pulse/task.h>
#include <pulse/timer.h>
#include <pulse/port.h>
#include <pulse/token_queue.h>

#include <stm32f1xx_hal.h>
#include <stm32f103xb.h>

#include <uart.h>

#include <etl/to_string.h>


using namespace pulse;


#define LED_Pin GPIO_PIN_13
#define LED_GPIO_Port GPIOC

#define BUTTON_Pin GPIO_PIN_14
#define BUTTON_GPIO_Port GPIOC


void
Panic(const char *msg)
{
    pulsePort_DisableInterrupts();
    uart.PanicFlush();
    for(;;);
}

void
MallocLock()
{
    pulsePort_EnterCriticalSection();
}

void
MallocUnlock()
{
    pulsePort_ExitCriticalSection();
}

namespace {

MallocUnit heap[16 * 1024 / sizeof(MallocUnit)];

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
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);

    init.Pin = LED_Pin;
    init.Mode = GPIO_MODE_OUTPUT_OD;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_Port, &init);
}

void
InitButton()
{
    GPIO_InitTypeDef init = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    init.Pin = BUTTON_Pin;
    init.Mode = GPIO_MODE_IT_FALLING;
    init.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(BUTTON_GPIO_Port, &init);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 8, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void
LedOn()
{
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
}

void
LedOff()
{
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
}

void
LedToggle()
{
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
}

bool
IsButtonPressed()
{
    // Active low.
    return HAL_GPIO_ReadPin(BUTTON_GPIO_Port, BUTTON_Pin) == GPIO_PIN_RESET;
}

Timer blinkTimer;
int blinkInterval = 0;
TokenQueue buttonEvents(5);

Awaitable<void>
ConfirmSwitch(int interval)
{
    LedOff();
    co_await Timer::Delay(etl::chrono::milliseconds(500));
    for (int i = 0; i <= interval; i++) {
        LedOn();
        co_await Timer::Delay(etl::chrono::milliseconds(100));
        LedOff();
        co_await Timer::Delay(etl::chrono::milliseconds(200));
    }
    co_await Timer::Delay(etl::chrono::milliseconds(500));
}

TaskV
BlinkTask()
{
    int interval = blinkInterval;

    while (true) {
        if (interval != blinkInterval) {
            interval = blinkInterval;
            co_await ConfirmSwitch(interval);
            continue;
        }
        blinkTimer.ExpiresAfter(etl::chrono::milliseconds(250 << interval));
        if (!co_await blinkTimer) {
            // Timer cancelled, so interval is definitely changed
            interval = blinkInterval;
            co_await ConfirmSwitch(interval);
            LedOn();
        } else {
            LedToggle();
        }
    }
}

TaskV
ButtonTask()
{
    constexpr auto JITTER_DELAY = etl::chrono::milliseconds(100);
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

        blinkInterval++;
        if (blinkInterval > 3) {
            blinkInterval = 0;
        }
        blinkTimer.Cancel();

        etl::string<16> s;
        uart.Write("New interval: ");
        uart.Write(etl::to_string(blinkInterval, s));
        uart.Write("\n");
        MallocStats stats;
        get_malloc_stats(&stats);
        uart.Write("Total free: ");
        uart.Write(etl::to_string(stats.totalFree, s));
        uart.Write("\n");
        uart.Write("Total used: ");
        uart.Write(etl::to_string(stats.totalUsed, s));
        uart.Write("\n");
        uart.Write("Min free: ");
        uart.Write(etl::to_string(stats.minFree, s));
        uart.Write("\n");
        uart.Write("Blocks allocated: ");
        uart.Write(etl::to_string(stats.numBlocksAllocated, s));
        uart.Write("\n");
    }
}

} /* anonymous namespace */

extern "C" void
EXTI15_10_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(BUTTON_Pin);
}

void
HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == BUTTON_Pin) {
        buttonEvents.Push();
    }
}

extern "C" int
main()
{
    pulse_add_heap_region(heap, sizeof(heap));

    HAL_Init();
    SystemClock_Config();

    static uint8_t uartBuffer[1024];
    uart.Initialize(230400, uartBuffer, sizeof(uartBuffer));

    uart.Write("Application started\n");

    InitLed();
    InitButton();

    auto blinkTask = Task::Spawn(BlinkTask());
    auto buttonTask = Task::Spawn(ButtonTask());

    Task::RunScheduler();

    Panic("Scheduler exited");
    return 0;
}


#ifdef __clang__
extern "C" void
_init()
{}

extern "C" void
_fini()
{}
#endif // __clang__
