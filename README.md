# ![Pulse](./docs/logo.svg)

Pulse is a C++20 coroutine framework for embedded applications. It provides a minimal set of
components required to build coroutine-based systems on resource-constrained devices. The framework
includes a lightweight dynamic memory allocator optimized for very small MCUs, a coroutine support
library, a priority-based scheduler, timer facilities, and a collection of essential components. It
does not attempt to replace the target MCU’s low-level hardware API or HAL; rather, it provides the
necessary infrastructure for convenient use of C++ coroutines in an embedded environment.

_DISCLAIMER: This project is still a work in progress and has not yet been fully battle-tested, so
the API may evolve to better suit real-world use cases. However, it is supported by extensive unit
test coverage, which helps ensure a solid level of quality._

_I plan to use it in my own embedded projects to identify issues and further refine the API.
Feedback is welcome, and others are encouraged to experiment with it — primarily for learning or
evaluation purposes rather than production use at this stage. Achieving production readiness will
likely require validation through several successful real-world deployments._


## Motivation

Cooperative scheduling eliminates the need for separate task stacks, as used in traditional RTOSes,
where each task requires a statically sized stack with additional safety margin. In practice, these
stacks are rarely utilized to their full capacity simultaneously, leading to inefficient RAM
usage — an important concern for resource-constrained MCUs.

In addition, the absence of implicit preemption significantly simplifies reasoning about thread
safety. Since suspension points are explicitly defined, state transitions become predictable and
easier to control, improving overall system correctness. Furthermore, coroutine state is managed by
the compiler, which preserves only the minimal required context at suspension points. Unlike
preemptive systems, this avoids saving a full set of CPU registers, reducing both memory footprint
and context-switch overhead.

Finally, the use of `co_await` can significantly simplify many common embedded algorithms by
streamlining control flow and improving code clarity and readability.

At present, there appears to be no widely adopted open-source C++ coroutine framework specifically
designed for embedded applications. In contrast, for example, the Rust ecosystem has
[Embassy](https://embassy.dev) framework, which pursue a similar goal and have demonstrated some
success. Pulse aims to fill this gap in the C++ domain.


## Configuration

Pulse can be configured at compile time via a `pulse_config.h` file, which defines the required
parameters. Most options have reasonable defaults, so in typical use only a small subset needs to be
specified. See the [configuration reference](docs/configuration.md) for details.


## Standard templates library

Pulse uses [ETL](https://github.com/ETLCPP/etl) as its standard template library. Users of the
framework are strongly encouraged to adopt ETL as well in order to share common library code and
ensure consistency across components. Some APIs also expose ETL types in their interfaces.


## Memory allocator

C++ coroutines require dynamic memory allocation for coroutine frames, so Pulse includes a dedicated
memory allocator. The allocator is a boundary-tag, free-list–based dynamic memory manager designed
for resource-constrained and real-time systems. It is highly configurable at compile time, allowing
it to be tailored to specific memory constraints.

Its compile-time configuration enables fine-grained trade-offs between maximum block size,
allocation granularity, and per-block overhead, which is a distinctive feature of the design. This
flexibility allows the allocator to be optimized for a wide range of target platforms with different
memory limitations. Allocation overhead can be as low as two bytes per block, making it suitable
even for very small MCUs with only hundreds of bytes of RAM. The allocator supports both first-fit
and best-fit strategies and can operate across multiple heap regions. Arbitrary memory regions can
be added at runtime, enabling flexible placement (e.g., across different SRAM banks).

The allocator is implemented as a self-contained module and can be used independently of Pulse. It
may be extracted into a dedicated submodule in the future.


## Examples

The [`examples` directory](./examples/) contains sample applications. See also [this extensive
tutorial](https://github.com/vagran/pulse-tutorial). Below are a few short snippets illustrating the
framework’s look and feel.


### Blinky

To blink a LED, simply spawn a task and use the provided timer facility to introduce delays.

```cpp
#include <pulse/task.h>
#include <pulse/timer.h>


// Declaring heap like this ensures proper alignment and granularity.
pulse::MallocUnit heap[HEAP_UNITS_SIZE_KB(1)];

pulse::TaskV
BlinkTask()
{
    while (true) {
        co_await pulse::Timer::Delay(etl::chrono::seconds(1));
        LedToggle();
    }
}

extern "C" [[noreturn]] void
main()
{
    // Add some RAM to the memory allocator.
    pulse::AddHeapRegion(heap, sizeof(heap));

    // Do some hardware initialization
    InitHardware();

    pulse::Task::Spawn(BlinkTask()).Pin();
    pulse::Task::RunScheduler(); // Never returns

    Panic("Scheduler exited");
}
```


### Advanced blinky

The following example demonstrates a slightly more advanced LED blinking application, implementing
the following behavior:

- The LED is toggled at an interval determined by the current mode: 0.25 s, 0.5 s, 1 s, or 2 s.
- The mode is cycled on each button press.
- Mode changes are visually indicated by N rapid LED blinks, where N corresponds to the mode index.
- The button input includes software debouncing.

```cpp
pulse::Timer blinkTimer;
int blinkIntervalIndex = 0;
// Published from the button ISR
pulse::TokenQueue<uint8_t> buttonEvents(5);

/// Make mode switch confirmation indication
pulse::Awaitable<void>
ConfirmSwitch(int intervalIndex)
{
    LedOff();
    co_await pulse::Timer::Delay(etl::chrono::milliseconds(500));
    for (int i = 0; i <= intervalIndex; i++) {
        LedOn();
        co_await pulse::Timer::Delay(etl::chrono::milliseconds(100));
        LedOff();
        co_await pulse::Timer::Delay(etl::chrono::milliseconds(200));
    }
    co_await pulse::Timer::Delay(etl::chrono::milliseconds(500));
}

// Handles LED blinking
pulse::TaskV
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
pulse::TaskV
ButtonTask()
{
    constexpr auto JITTER_DELAY = etl::chrono::milliseconds(20);
    pulse::Timer jitterTimer;

    while (true) {
        co_await buttonEvents;
        // Suppress jitter - wait until active level is stable for a long period.
        bool pressed = false;
        while (true) {
            jitterTimer.ExpiresAfter(JITTER_DELAY);
            size_t idx = co_await pulse::Task::WhenAny(buttonEvents, jitterTimer);
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
    }
}

extern "C" void
EXTI15_10_IRQHandler()
{
    HAL_GPIO_EXTI_IRQHandler(ioButton.pin);
}

void
HAL_GPIO_EXTI_Callback(uint16_t gpioPin)
{
    if (gpioPin == ioButton.pin) {
        buttonEvents.Push();
    }
}

extern "C" [[noreturn]] void
main()
{
    pulse::AddHeapRegion(heap, sizeof(heap));

    InitHardware();

    pulse::Task::Spawn(BlinkTask()).Pin();
    pulse::Task::Spawn(ButtonTask()).Pin();

    pulse::Task::RunScheduler();

    Panic("Scheduler exited");
}

```


## Porting

Pulse can be ported to any target platform with a working C++20 compiler. At present, it has been
primarily tested on STM32-series MCUs. The provided examples target the STM32F103 development board
("Blue Pill"). See the [porting guide](docs/porting.md) for details.


## License

Pulse is licensed under MIT license.
