#include <catch2/catch_test_macros.hpp>
#include <pulse/timer.h>
#include <functional>
#include <format>
#include <iostream>


using namespace pulse;


namespace {

constexpr Timer::TickCount TICK_FREQ = pulseConfig_TICK_FREQ;

struct TestEntry {
    // Should return true if timer wait succeeded, false if cancelled.
    std::function<Awaitable<bool>()> testFunc;
    Timer::TickCount fireTime;
    bool cancelExpected = false;

    bool complete = false;
    Awaitable<bool> awaitable;
};

void
TestTimer(std::vector<TestEntry> tests,
          Timer::TickCount startTime = 0)
{
    Timer::SetTime(startTime);

    auto CheckComplete = [&]() -> Awaitable<bool> {
        int numIncomplete = 0;
        Timer::TickCount curTime = Timer::GetTime();
        for (auto &e: tests) {
            if (!e.complete) {
                if (e.awaitable.IsFinished()) {
                    if (Timer::IsBefore(curTime, e.fireTime)) {
                        FAIL(std::format("Timer is fired earlier than expected: {} < {}",
                                         curTime, e.fireTime));
                    }
                    bool result = co_await e.awaitable;
                    if (result != !e.cancelExpected) {
                        if (result) {
                            FAIL("Unexpected timer cancellation");
                        } else {
                            FAIL("Timer not cancelled as expected");
                        }
                    }
                    e.complete = true;
                } else {
                    if (!Timer::IsBefore(curTime, e.fireTime)) {
                        FAIL(std::format("Timer not fired when expected: {}", curTime));
                    }
                    numIncomplete++;
                }
            }
        }
        co_return numIncomplete == 0;
    };

    auto Main = [&]() -> TaskV {
        for (auto &e: tests) {
            e.awaitable = e.testFunc();
        }
        while (!co_await CheckComplete()) {
            Timer::Tick();
            details::CheckTimers();
            co_await Task::Switch();
        }
    };

    auto task = Task::Spawn(Main());
    Task::RunSome();
    REQUIRE(task.IsFinished());
    for (auto &e: tests) {
        REQUIRE(e.complete);
    }
    std::cout << Timer::GetTime() - startTime << " ticks emulated\n";
}

} // anonymous namespace


TEST_CASE("Basic delay - single tick")
{
    auto MakeDelay = []() -> Awaitable<bool> {
        co_await Timer::Delay(1);
        co_return true;
    };

    TestTimer({TestEntry{MakeDelay, 1}});
}


TEST_CASE("Basic delay")
{
    auto MakeDelay = []() -> Awaitable<bool> {
        co_await Timer::Delay(etl::chrono::seconds(1));
        co_return true;
    };

    TestTimer({TestEntry{MakeDelay, TICK_FREQ}});
}


TEST_CASE("Basic delay - time point")
{
    auto MakeDelay = []() -> Awaitable<bool> {
        co_await Timer::WaitUntil(200);
        co_return true;
    };

    TestTimer({TestEntry{MakeDelay, 200}}, 100);
}


TEST_CASE("Basic delay - time point in past")
{
    auto MakeDelay = []() -> Awaitable<bool> {
        co_await Timer::WaitUntil(50);
        co_return true;
    };

    TestTimer({TestEntry{MakeDelay, 100}}, 100);
}
