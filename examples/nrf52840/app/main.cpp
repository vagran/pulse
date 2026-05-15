#include <pulse/port.h>


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
    // CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    // DWT->CYCCNT = 0;
    // DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    // if (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) {
    //     /* 1 second delay. */
    //     uint32_t cycles = SystemCoreClock;
    //     uint32_t start = DWT->CYCCNT;
    //     while ((DWT->CYCCNT - start) < cycles);
    // }

    //XXX
    // NVIC_SystemReset();
#endif

    //XXX
    for(;;);
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


#define NRF_P0_BASE 0x50000000UL

#define P0_OUT (*(volatile uint32_t *)(NRF_P0_BASE + 0x504))
#define P0_OUTSET (*(volatile uint32_t *)(NRF_P0_BASE + 0x508))
#define P0_OUTCLR (*(volatile uint32_t *)(NRF_P0_BASE + 0x50C))
#define P0_DIRSET (*(volatile uint32_t *)(NRF_P0_BASE + 0x518))
#define P0_PIN_CNF(pin) (*(volatile uint32_t *)(NRF_P0_BASE + 0x700 + ((pin) * 4)))

#define LED_PIN 15 // P0.13 = LED1 on nRF52840 DK

namespace {

// PIN_CNF fields:
// [1:0] DIR     1 = output
// [2]   INPUT   disconnect for output
// [10:8] PULL   disabled
// [17:16] DRIVE standard
// [18]  SENSE   disabled
static void
gpio_init(void)
{
    P0_PIN_CNF(LED_PIN) = (1 << 0) | // DIR = Output
                          (1 << 1);  // INPUT = Disconnect

    P0_DIRSET = (1UL << LED_PIN);

    // LED off initially (active low)
    P0_OUTSET = (1UL << LED_PIN);
}

static void
delay(uint32_t count)
{
    while (count--) {
        __asm__ volatile("nop");
    }
}

} // anonymous namespace

extern "C" [[noreturn]] int
main()
{
    gpio_init();

    while (1) {
        // LED ON (active low)
        P0_OUTCLR = (1UL << LED_PIN);
        delay(500000);

        // LED OFF
        P0_OUTSET = (1UL << LED_PIN);
        delay(500000);
    }
}

extern "C" void
_init()
{}

extern "C" void
_fini()
{}
