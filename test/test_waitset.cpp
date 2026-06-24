#include <catch2/catch_test_macros.hpp>
#include <pulse/waitset.h>
#include <pulse/token_queue.h>
#include <pulse/event.h>
#include <pulse/discard_queue.h>

#include <etl/optional.h>


using namespace pulse;


namespace {

/// Result type whose live instances are counted, to verify retained results are properly destroyed.
struct Tracked {
    static inline int liveCount = 0;
    int value = 0;

    Tracked() { liveCount++; }
    Tracked(int value): value(value) { liveCount++; }
    Tracked(const Tracked &other): value(other.value) { liveCount++; }
    Tracked(Tracked &&other): value(other.value) { liveCount++; }
    ~Tracked() { liveCount--; }
};

} // anonymous namespace


TEST_CASE("Basic")
{
    TokenQueue<> q0, q1, q2;
    Event e;
    bool done = false;

    auto waitset = CreateWaitset(
        [&](){ return q0.Take(); },
        [&](){ return q1.Take(); },
        [&](){ return q2.Take(); },
        [&](){ return e.Wait(); }
    );

    auto consumer = tasks::Spawn([](decltype(waitset) &waitset, bool &done) -> Task<> {
        std::vector<std::tuple<size_t, size_t>> expected = {
            {2, 0},
            {1, 0},
            {2, 1},
            {0, 0},

            {1, 1},
            {2, 2},

            {2, 3},
            // It wakes on q2, then q1 and q0 pushed, and q0 is taken first as it has lower index.
            {0, 1},
            {1, 2}
        };

        for (size_t iterIdx = 0; ; iterIdx++) {

            size_t idx = co_await waitset;

            if (idx == 3) {
                REQUIRE(iterIdx == expected.size());
                REQUIRE(waitset.HasResult(3));
                waitset.ClearResult<3>();
                REQUIRE(!waitset.HasResult(3));
                break;
            }

            REQUIRE(iterIdx < expected.size());
            auto &e = expected[iterIdx];
            REQUIRE(idx == std::get<0>(e));
            REQUIRE(waitset.HasResult(idx));
            switch (idx) {
            case 0:
                REQUIRE(waitset.Result<0>() == std::get<1>(e));
                waitset.ClearResult<0>();
                break;
            case 1:
                REQUIRE(waitset.Result<1>() == std::get<1>(e));
                waitset.ClearResult<1>();
                break;
            case 2:
                REQUIRE(waitset.Result<2>() == std::get<1>(e));
                REQUIRE(waitset.PopResult<2>() == std::get<1>(e));
                break;
            default:
                FAIL("Bad index");
            }
            REQUIRE(!waitset.HasResult(idx));
        }

        done = true;
    }, waitset, done);

    auto producer = tasks::Spawn([](TokenQueue<> &q0, TokenQueue<> &q1, TokenQueue<> &q2,
                                    Event &e) -> Task<> {
        q2.Push();
        while (co_await tasks::Switch());
        q1.Push();
        while (co_await tasks::Switch());
        q2.Push();
        while (co_await tasks::Switch());
        q0.Push();
        while (co_await tasks::Switch());

        q1.Push();
        q2.Push();
        while (co_await tasks::Switch());

        q2.Push();
        q1.Push();
        q0.Push();
        while (co_await tasks::Switch());

        e.Set();

    }, q0, q1, q2, e);

    tasks::RunSome();

    REQUIRE(done);
    REQUIRE(consumer.IsFinished());
    REQUIRE(producer.IsFinished());
}


TEST_CASE("Retained non-trivial result is destroyed with the waitset")
{
    // A waitset retains a completed awaiter's result until it is popped/cleared. If the waitset is
    // destroyed while a result is still parked in a slot, that result must be destroyed too (the
    // slot stores it in a manually-managed union). A non-trivial Tracked result makes a missed
    // destructor observable here, and a leak/UB observable under ASAN.
    Tracked::liveCount = 0;
    {
        InlineDiscardQueue<Tracked, true, 4> dq;

        auto factory = [&](){ return dq.Pop(); };
        using WS = Waitset<decltype(factory)>;

        etl::optional<WS> ws;
        ws.emplace(factory);

        // Item is ready before the consumer awaits, so the handler completes synchronously and the
        // result is parked in slot 0.
        dq.Emplace(42);

        auto consumer = tasks::Spawn([](etl::optional<WS> &ws) -> Task<> {
            size_t idx = co_await *ws;
            REQUIRE(idx == 0);
            REQUIRE(ws->HasResult(0));
            REQUIRE(ws->Result<0>().value == 42);
            // Intentionally leave the result in the slot - it must be cleaned up on destruction.
        }, ws);

        tasks::RunSome();

        REQUIRE(consumer.IsFinished());
        // Exactly one instance retained inside the slot.
        REQUIRE(Tracked::liveCount == 1);

        ws.reset();
        // Destroying the waitset must run the retained result's destructor.
        REQUIRE(Tracked::liveCount == 0);
    }
    REQUIRE(Tracked::liveCount == 0);
}


TEST_CASE("Destruction wakes a pending waiter with DESTRUCTED")
{
    TokenQueue<> q;

    auto factory = [&](){ return q.Take(); };
    using WS = Waitset<decltype(factory)>;

    etl::optional<WS> ws;
    ws.emplace(factory);

    size_t got = 12345;

    // Nothing is ready, so this consumer suspends inside the waitset.
    auto consumer = tasks::Spawn([](etl::optional<WS> &ws, size_t &got) -> Task<> {
        got = co_await *ws;
    }, ws, got);

    // Destroy the waitset while the consumer is still suspended on it.
    auto destroyer = tasks::Spawn([](etl::optional<WS> &ws) -> Task<> {
        ws.reset();
        co_return;
    }, ws);

    tasks::RunSome();

    REQUIRE(consumer.IsFinished());
    REQUIRE(destroyer.IsFinished());
    REQUIRE(got == WS::DESTRUCTED);
}


TEST_CASE("Simultaneously ready slots are drained lowest-index-first")
{
    TokenQueue<> q0, q1;

    auto ws = CreateWaitset(
        [&](){ return q0.Take(); },
        [&](){ return q1.Take(); }
    );

    // Both slots become ready before anything is awaited.
    q0.Push();
    q1.Push();

    auto consumer = tasks::Spawn([](decltype(ws) &ws) -> Task<> {
        // First wait reports the lowest ready index.
        size_t a = co_await ws;
        REQUIRE(a == 0);
        REQUIRE(ws.HasResult(0));
        ws.ClearResult<0>();

        // Slot 1 is still ready - the second wait must surface it from the leftover readyMask
        // without any producer running again.
        size_t b = co_await ws;
        REQUIRE(b == 1);
        REQUIRE(ws.HasResult(1));
        ws.ClearResult<1>();
    }, ws);

    tasks::RunSome();

    REQUIRE(consumer.IsFinished());
}


TEST_CASE("A disabled slot is not awaited and EnableSlot re-arms it")
{
    TokenQueue<> q0, q1;

    auto ws = CreateWaitset(
        [&](){ return q0.Take(); },
        [&](){ return q1.Take(); }
    );

    // Slot 0 is disabled, so it must not be armed even though its queue has data.
    ws.DisableSlot<0>();

    q0.Push();
    q1.Push();

    auto consumer = tasks::Spawn([](decltype(ws) &ws) -> Task<> {
        // Slot 0 is disabled, so only slot 1 is awaited - its index is reported despite slot 0
        // also having a ready token.
        size_t a = co_await ws;
        REQUIRE(a == 1);
        REQUIRE(!ws.HasResult(0));
        ws.ClearResult<1>();

        // Re-enable slot 0; the token pushed earlier is still queued and must now be picked up.
        ws.EnableSlot<0>();
        size_t b = co_await ws;
        REQUIRE(b == 0);
        REQUIRE(ws.HasResult(0));
        ws.ClearResult<0>();
    }, ws);

    tasks::RunSome();

    REQUIRE(consumer.IsFinished());
}


TEST_CASE("DisableSlot discards an already-scheduled completion")
{
    // When a slot's awaitable completes, its handler coroutine is scheduled (the runnable queue
    // holds only a weak reference to it). Disabling the slot releases the handler's last strong
    // reference, destroying the coroutine frame - so the scheduler skips it and the completion is
    // never delivered. Any result already moved into the handler chain is destroyed with the frame.
    Tracked::liveCount = 0;
    {
        InlineDiscardQueue<Tracked, true, 4> dqA, dqB;

        auto fa = [&](){ return dqA.Pop(); };
        auto fb = [&](){ return dqB.Pop(); };
        using WS = Waitset<decltype(fa), decltype(fb)>;

        etl::optional<WS> ws;
        ws.emplace(fa, fb);

        bool woke = false;
        size_t wokeIdx = 999;

        auto consumer = tasks::Spawn(
            [](etl::optional<WS> &ws, bool &woke, size_t &wokeIdx) -> Task<> {
                wokeIdx = co_await *ws;
                woke = true;
            }, ws, woke, wokeIdx);

        // Both handlers are armed and suspended on their (empty) queues.
        tasks::RunSome();
        REQUIRE(!woke);

        // Slot 0 completes: its handler is now scheduled and the produced value lives in the
        // handler chain.
        dqA.Emplace(7);
        REQUIRE(Tracked::liveCount == 1);

        // Disable slot 0 before the scheduler gets to run the handler. The in-flight result must be
        // destroyed immediately with the handler frame.
        ws->DisableSlot<0>();
        REQUIRE(Tracked::liveCount == 0);

        // The scheduler must skip the now-dead handler - no completion is delivered.
        tasks::RunSome();
        REQUIRE(!woke);
        REQUIRE(!ws->HasResult(0));

        // Slot 1 is still active and can still wake the consumer.
        dqB.Emplace(9);
        tasks::RunSome();
        REQUIRE(woke);
        REQUIRE(wokeIdx == 1);
        REQUIRE(ws->HasResult(1));
        REQUIRE(ws->Result<1>().value == 9);

        // Slot 1's result is retained until the waitset is destroyed.
        REQUIRE(Tracked::liveCount == 1);
        ws.reset();
        REQUIRE(Tracked::liveCount == 0);
    }
    REQUIRE(Tracked::liveCount == 0);
}


TEST_CASE("DisableSlot preserves an already-saved result")
{
    TokenQueue<> q;

    auto ws = CreateWaitset([&](){ return q.Take(); });

    q.Push();

    auto consumer = tasks::Spawn([](decltype(ws) &ws) -> Task<> {
        // Slot produces a result that gets saved in the waitset.
        size_t a = co_await ws;
        REQUIRE(a == 0);
        REQUIRE(ws.HasResult(0));

        // Disabling the slot must preserve the already-saved result...
        ws.DisableSlot<0>();
        REQUIRE(ws.HasResult(0));

        // ...and WaitAny keeps returning it until it is explicitly cleared.
        size_t b = co_await ws;
        REQUIRE(b == 0);
        REQUIRE(ws.HasResult(0));

        ws.ClearResult<0>();
        REQUIRE(!ws.HasResult(0));
    }, ws);

    tasks::RunSome();

    REQUIRE(consumer.IsFinished());
}
