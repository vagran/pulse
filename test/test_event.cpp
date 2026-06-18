#include <catch2/catch_test_macros.hpp>
#include <pulse/event.h>


using namespace pulse;


namespace {

std::vector<std::string> results;

} // anonymous namespace


TEST_CASE("Event state is sticky and can be re-armed")
{
    Event ev;

    // Set()/Unset() return the previous state.
    REQUIRE(ev.Set() == false);
    // Once set it stays set, so a second Set() sees the previous (set) state.
    REQUIRE(ev.Set() == true);
    REQUIRE(ev.Unset() == true);
    REQUIRE(ev.Unset() == false);

    // Constructed in set state.
    Event evSet(true);
    REQUIRE(evSet.Unset() == true);
}

TEST_CASE("Event await on set event resumes immediately")
{
    results.clear();

    Event ev(true);

    auto t1 = tasks::Spawn([](Event &ev) -> Task<> {
        co_await ev;
        results.push_back("done");
    }, ev);

    tasks::RunSome();

    REQUIRE(t1.IsFinished());
    REQUIRE(results.size() == 1);
}

TEST_CASE("Event await blocks until set")
{
    results.clear();

    Event ev;

    auto waiter = tasks::Spawn([](Event &ev) -> Task<> {
        co_await ev;
        results.push_back("woken");
    }, ev);

    auto setter = tasks::Spawn([](Event &ev) -> Task<> {
        // Waiter must be suspended on the unset event by now.
        REQUIRE(results.empty());
        ev.Set();
        co_return;
    }, ev);

    tasks::RunSome();

    REQUIRE(waiter.IsFinished());
    REQUIRE(setter.IsFinished());
    REQUIRE(results.size() == 1);
    REQUIRE(results.back() == "woken");
}

TEST_CASE("Event set wakes all waiters")
{
    results.clear();

    Event ev;

    auto Waiter = [](Event &ev, const char *id) -> Task<> {
        co_await ev;
        results.push_back(id);
    };

    auto w1 = tasks::Spawn(Waiter, ev, "W1");
    auto w2 = tasks::Spawn(Waiter, ev, "W2");
    auto w3 = tasks::Spawn(Waiter, ev, "W3");

    auto setter = tasks::Spawn([](Event &ev) -> Task<> {
        // All three waiters are queued and still blocked.
        REQUIRE(results.empty());
        // A single Set() must release every waiter (manual-reset semantics),
        // unlike a semaphore which would wake one per token.
        ev.Set();
        co_return;
    }, ev);

    tasks::RunSome();

    REQUIRE(w1.IsFinished());
    REQUIRE(w2.IsFinished());
    REQUIRE(w3.IsFinished());
    REQUIRE(setter.IsFinished());
    REQUIRE(results.size() == 3);
}

TEST_CASE("Event await after unset blocks again")
{
    results.clear();

    Event ev(true);

    auto t1 = tasks::Spawn([](Event &ev) -> Task<> {
        // First wait passes through (event is set).
        co_await ev;
        results.push_back("first");
        ev.Unset();
        // Second wait must block until the event is set again.
        co_await ev;
        results.push_back("second");
    }, ev);

    auto setter = tasks::Spawn([](Event &ev) -> Task<> {
        // Let t1 run up to its second (blocking) wait.
        while (co_await tasks::Switch());
        REQUIRE(results.size() == 1);
        REQUIRE(results.back() == "first");
        ev.Set();
    }, ev);

    tasks::RunSome();

    REQUIRE(t1.IsFinished());
    REQUIRE(setter.IsFinished());
    REQUIRE(results.size() == 2);
    REQUIRE(results.back() == "second");
}

TEST_CASE("Event WhenAny releases the unfired awaiter")
{
    Event ev1, ev2;

    auto t1 = tasks::Spawn([](Event &ev1, Event &ev2) -> Task<int> {
        // Only ev2 fires; the ev1 awaiter is never resumed and must be
        // dequeued cleanly (verifiable under ASAN/Valgrind).
        co_return co_await tasks::WhenAny(ev1, ev2);
    }, ev1, ev2);

    auto t2 = tasks::Spawn([](Event &ev2, Task<int> t1) -> Task<> {
        ev2.Set();
        int idx = co_await t1;
        REQUIRE(idx == 1);
    }, ev2, t1);

    tasks::RunSome();

    REQUIRE(t2.IsFinished());
}

TEST_CASE("Event destruction wakes pending waiters")
{
    results.clear();

    etl::optional<Event> ev;
    ev.emplace();

    auto t1 = tasks::Spawn([](etl::optional<Event> &ev) -> Task<> {
        co_await *ev;
        results.push_back("resumed");
    }, ev);

    auto t2 = tasks::Spawn([](etl::optional<Event> &ev) -> Task<> {
        // Destroy the event while a task is still waiting on it.
        ev.reset();
        co_return;
    }, ev);

    tasks::RunSome();

    REQUIRE(t1.IsFinished());
    REQUIRE(t2.IsFinished());
    REQUIRE(results.size() == 1);
}
