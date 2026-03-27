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
    // Returns true if timer wait succeeded, false if cancelled.
    Awaitable<bool> awaitable;
};

void
TestTimer(std::vector<TestEntry> tests)
{
    Timer::TickCount startTime = Timer::GetTime();

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
                            FAIL("Timer not cancelled as expected");
                        } else {
                            FAIL("Unexpected timer cancellation");
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
            while (co_await Task::Switch());
            Timer::Tick();
            details::CheckTimers();
            while (co_await Task::Switch());
        }
    };

    auto task = Task::Spawn(Main(), Task::LOWEST_PRIORITY);
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
    Timer::SetTime(0);

    auto MakeDelay = []() -> Awaitable<bool> {
        co_await Timer::Delay(1);
        co_return true;
    };

    TestTimer({TestEntry{MakeDelay, 1}});
}


TEST_CASE("Basic delay")
{
    Timer::SetTime(0);

    auto MakeDelay = []() -> Awaitable<bool> {
        co_await Timer::Delay(etl::chrono::seconds(1));
        co_return true;
    };

    TestTimer({TestEntry{MakeDelay, TICK_FREQ}});
}


TEST_CASE("Basic delay - time point")
{
    Timer::SetTime(100);

    auto MakeDelay = []() -> Awaitable<bool> {
        co_await Timer::WaitUntil(200);
        co_return true;
    };

    TestTimer({TestEntry{MakeDelay, 200}});
}


TEST_CASE("Basic delay - time point in past")
{
    Timer::SetTime(100);

    auto MakeDelay = []() -> Awaitable<bool> {
        co_await Timer::WaitUntil(50);
        co_return true;
    };

    TestTimer({TestEntry{MakeDelay, 100}});
}


TEST_CASE("Basic delay - multiple delays")
{
    Timer::SetTime(0);

    auto MakeDelay1 = []() -> Awaitable<bool> {
        co_await Timer::WaitUntil(5);
        co_return true;
    };

    auto MakeDelay2 = []() -> Awaitable<bool> {
        co_await Timer::WaitUntil(10);
        co_return true;
    };

    TestTimer({TestEntry{MakeDelay1, 5}, TestEntry{MakeDelay2, 10}});
}


TEST_CASE("Basic timer")
{
    Timer::SetTime(0);

    auto MakeDelay = []() -> Awaitable<bool> {
        Timer timer(10);
        co_return co_await timer;
    };

    TestTimer({TestEntry{MakeDelay, 10}});
}


TEST_CASE("Timer - dynamic alloc")
{
    Timer::SetTime(0);
    auto timer = Timer::Create(etl::chrono::seconds(1));

    auto MakeDelay = [timer]() -> Awaitable<bool> {
        co_return co_await timer;
    };

    TestTimer({TestEntry{MakeDelay, TICK_FREQ}});
}


TEST_CASE("Timer - multiple waiters")
{
    Timer::SetTime(0);

    auto timer = Timer::Create(etl::chrono::seconds(1));

    auto MakeDelay1 = [timer]() -> Awaitable<bool> {
        co_return co_await timer;
    };

    auto MakeDelay2 = [timer]() -> Awaitable<bool> {
        co_return co_await timer;
    };

    auto MakeDelay3 = [timer]() -> Awaitable<bool> {
        co_return co_await timer;
    };

    TestTimer({TestEntry{MakeDelay1, TICK_FREQ}, TestEntry{MakeDelay2, TICK_FREQ},
              TestEntry{MakeDelay3, TICK_FREQ}});
}


TEST_CASE("Timer - repeated expiration")
{
    Timer::SetTime(0);

    auto timer = Timer::Create(2);

    auto MakeDelay2 = [timer]() -> TTask<bool> {
        REQUIRE(Timer::GetTime() == 2);
        co_return co_await timer;
    };

    auto MakeDelay1 = [timer, &MakeDelay2]() -> Awaitable<bool> {
        REQUIRE(co_await timer);
        REQUIRE(Timer::GetTime() == 2);
        timer->ExpiresAt(10);
        auto task = Task::Spawn(MakeDelay2(), Task::HIGHEST_PRIORITY);
        co_return co_await task;
    };

    TestTimer({TestEntry{MakeDelay1, 10}});
}


TEST_CASE("Timer - repeated expiration (awaitable func)")
{
    Timer::SetTime(0);

    auto timer = Timer::Create(2);

    auto MakeDelay2 = [timer]() -> Awaitable<bool> {
        co_return co_await timer;
    };

    auto MakeDelay1 = [timer, &MakeDelay2]() -> Awaitable<bool> {
        REQUIRE(co_await timer);
        REQUIRE(Timer::GetTime() == 2);
        timer->ExpiresAt(5);
        co_return co_await MakeDelay2();
    };

    TestTimer({TestEntry{MakeDelay1, 5}});
}


TEST_CASE("Cancelled timer")
{
    Timer::SetTime(0);

    auto MakeDelay = []() -> Awaitable<bool> {
        Timer timer(10);
        timer.Cancel();
        co_return co_await timer;
    };

    TestTimer({TestEntry{MakeDelay, 0, true}});
}


TEST_CASE("Cancel timer")
{
    Timer::SetTime(0);

    auto timer1 = Timer::Create(10);
    auto timer2 = Timer::Create(2);

    auto MakeDelay1 = [timer1]() -> Awaitable<bool> {
        co_return co_await timer1;
    };

    auto MakeDelay2 = [timer1, timer2]() -> Awaitable<bool> {
        bool ret = co_await timer2;
        timer1->Cancel();
        co_return ret;
    };

    TestTimer({TestEntry{MakeDelay1, 2, true}, TestEntry{MakeDelay2, 2}});
}


TEST_CASE("Cancel timer on re-schedule")
{
    Timer::SetTime(0);

    auto timer1 = Timer::Create(10);
    auto timer2 = Timer::Create(2);

    auto MakeDelay1 = [timer1]() -> Awaitable<bool> {
        co_return co_await timer1;
    };

    auto MakeDelay2 = [timer1, timer2]() -> Awaitable<bool> {
        bool ret = co_await timer2;
        timer1->ExpiresAt(20);
        co_return ret;
    };

    TestTimer({TestEntry{MakeDelay1, 2, true}, TestEntry{MakeDelay2, 2}});
}


TEST_CASE("Timer move")
{
    Timer::SetTime(0);

    auto MakeDelay = []() -> Awaitable<bool> {
        Timer timer(10);

        Timer timer2(etl::move(timer));

        co_return co_await timer2;
    };

    TestTimer({TestEntry{MakeDelay, 10}});
}


TEST_CASE("Move waited timer")
{
    Timer::SetTime(0);

    Timer timer1(10);
    Timer timer2(2);
    std::optional<Timer> timer3;


    auto MakeDelay1 = [&timer1]() -> Awaitable<bool> {
        co_return co_await timer1;
    };

    auto MakeDelay2 = [&timer1, &timer2, &timer3]() -> Awaitable<bool> {
        REQUIRE(co_await timer2);
        REQUIRE(Timer::GetTime() == 2);
        timer3.emplace(etl::move(timer1));
        co_return co_await timer3->Wait();
    };

    TestTimer({TestEntry{MakeDelay1, 2, true}, TestEntry{MakeDelay2, 10}});
}


TEST_CASE("Move time forward")
{
    Timer::SetTime(0);

    auto timer1 = Timer::Create(10);
    auto timer2 = Timer::Create(2);

    auto MakeDelay1 = [timer1]() -> Awaitable<bool> {
        co_return co_await timer1;
    };

    auto MakeDelay2 = [timer1, timer2]() -> Awaitable<bool> {
        bool ret = co_await timer2;
        REQUIRE(Timer::GetTime() == 2);
        Timer::SetTime(20);
        co_return ret;
    };

    TestTimer({TestEntry{MakeDelay1, 20}, TestEntry{MakeDelay2, 20}});
}
