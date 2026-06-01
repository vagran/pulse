#include <catch2/catch_test_macros.hpp>
#include <pulse/discard_queue.h>
#include <pulse/token_queue.h>
#include <pulse/timer.h>
#include <interrupts.h>
#include <random>
#include <thread>
#include <mutex>
#include <condition_variable>


using namespace pulse;

namespace {

std::atomic<size_t> numConstructed = 0, numDestructed = 0,
                    numValuesConstructed = 0, numValuesDestructed = 0;

void
ResetStats()
{
    numConstructed = 0;
    numDestructed = 0;
    numValuesConstructed = 0;
    numValuesDestructed = 0;
}

void
CheckStats()
{
    CHECK(numConstructed == numDestructed);
    CHECK(numValuesConstructed == numValuesDestructed);
}

class A {
public:
    static constexpr uint32_t INIT_SIG = 0xc001babe, FREE_SIG = 0xfeeefeee;

    // Use dynamic allocation to help troubleshooting with Valgrind.
    std::unique_ptr<int> value;
    uint32_t signature = INIT_SIG;

    A()
    {
        numConstructed++;
    }

    A(int value):
        value(std::make_unique<int>(value))
    {
        numConstructed++;
        numValuesConstructed++;
    }

    A(const A &other):
        value(std::make_unique<int>(*other.value))
    {
        numConstructed++;
        numValuesConstructed++;
    }

    A(A &&other):
        value(std::move(other.value))
    {
        numConstructed++;
    }

    ~A()
    {
        REQUIRE(signature == INIT_SIG);
        signature = FREE_SIG;
        numDestructed++;
        if (value) {
            numValuesDestructed++;
        }
    }

    A &
    operator =(A &&other)
    {
        if (value) {
            numValuesDestructed++;
        }
        value = std::move(other.value);
        return *this;
    }
};

class MoveableOnly {
public:
    // Use dynamic allocation to help troubleshooting with Valgrind.
    std::unique_ptr<int> value;

    MoveableOnly()
    {
        numConstructed++;
    }

    MoveableOnly(int value):
        value(std::make_unique<int>(value))
    {
        numConstructed++;
        numValuesConstructed++;
    }

    MoveableOnly(MoveableOnly &&other):
        value(std::move(other.value))
    {
        numConstructed++;
    }

    ~MoveableOnly()
    {
        numDestructed++;
        if (value) {
            numValuesDestructed++;
        }
    }
};


std::vector<std::string> results;

void
CheckResult(size_t expectedSize, const std::string &expectedLast)
{
    REQUIRE(!results.empty());
    REQUIRE(expectedLast == results.back());
    REQUIRE(expectedSize == results.size());
}

class Scheduler {
public:
    void
    Terminate()
    {
        std::unique_lock lock(mutex);
        isDone = true;
        cv.notify_all();
    }

    void
    Wakeup()
    {
        std::unique_lock lock(mutex);
        isPending = true;
        cv.notify_all();
    }

    void
    Run()
    {
        while (true) {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&](){ return isDone || isPending; });
            isPending = false;
            if (isDone) {
                break;
            }
            lock.unlock();
            tasks::RunSome();
        }
        tasks::RunSome();
    }
private:
    bool isDone = false, isPending = false;
    std::mutex mutex;
    std::condition_variable cv;
};

} // anonymous namespace


TEST_CASE("Discard queue")
{
    ResetStats();
    results.clear();

    {
        SECTION("Basic") {
            InlineDiscardQueue<A, true, 2> q;

            auto t1 = tasks::Spawn([](InlineDiscardQueue<A, true, 2> &q) -> Task<> {
                REQUIRE(results.empty());
                results.push_back("T1:1");

                REQUIRE(q.Push(1));
                REQUIRE(q.Push(2));
                REQUIRE(!q.Push(3));

                while (co_await tasks::Switch()) {}

                CheckResult(4, "T2:3");
                results.push_back("T1:2");
                REQUIRE(q.Push(4));
            }, q);

            auto t2 = tasks::Spawn([](InlineDiscardQueue<A, true, 2> &q) -> Task<> {
                CheckResult(1, "T1:1");
                results.push_back("T2:1");
                {
                    A a = co_await q.Pop();
                    REQUIRE(*a.value == 1);
                }

                CheckResult(2, "T2:1");
                results.push_back("T2:2");
                {
                    A a = co_await q.Pop();
                    REQUIRE(*a.value == 2);
                }

                CheckResult(3, "T2:2");
                results.push_back("T2:3");
                {
                    A a = co_await q.Pop();
                    REQUIRE(*a.value == 4);
                }

                CheckResult(5, "T1:2");
                results.push_back("T2:4");
            }, q);

            tasks::RunSome();

            CheckResult(6, "T2:4");
        }

        SECTION("Save result") {
            InlineDiscardQueue<A, true, 2> q;
            etl::optional<A> result;

            auto t1 = tasks::Spawn([](InlineDiscardQueue<A, true, 2> &q,
                                     etl::optional<A> &result) -> Task<> {

                REQUIRE(!result);
                q.Push(42);
                while (co_await tasks::Switch());
                REQUIRE(*result->value == 42);

            }, q, result);

            auto t2 = tasks::Spawn([](InlineDiscardQueue<A, true, 2> &q,
                                     etl::optional<A> &result) -> Task<> {

                co_await tasks::SaveResult(q.Pop(), result);
                REQUIRE(*result->value == 42);

            }, q, result);

            tasks::RunSome();
            REQUIRE(*result->value == 42);
        }

        SECTION("Save result (lvalue awaiter)") {
            InlineDiscardQueue<A, true, 2> q;
            etl::optional<A> result;

            auto t1 = tasks::Spawn([](InlineDiscardQueue<A, true, 2> &q,
                                     etl::optional<A> &result) -> Task<> {

                REQUIRE(!result);
                q.Push(42);
                while (co_await tasks::Switch());
                REQUIRE(*result->value == 42);

            }, q, result);

            auto t2 = tasks::Spawn([](InlineDiscardQueue<A, true, 2> &q,
                                     etl::optional<A> &result) -> Task<> {
                auto awaiter = q.Pop();
                co_await tasks::SaveResult(awaiter, result);
                REQUIRE(*result->value == 42);

            }, q, result);

            tasks::RunSome();
            REQUIRE(*result->value == 42);
        }

        SECTION("Save result (MoveableOnly)") {
            InlineDiscardQueue<MoveableOnly, true, 2> q;
            etl::optional<MoveableOnly> result;

            auto t1 = tasks::Spawn([](InlineDiscardQueue<MoveableOnly, true, 2> &q,
                                     etl::optional<MoveableOnly> &result) -> Task<> {

                REQUIRE(!result);
                q.Push(42);
                while (co_await tasks::Switch());
                REQUIRE(*result->value == 42);

            }, q, result);

            auto t2 = tasks::Spawn([](InlineDiscardQueue<MoveableOnly, true, 2> &q,
                                     etl::optional<MoveableOnly> &result) -> Task<> {

                co_await tasks::SaveResult(q, result);
                REQUIRE(*result->value == 42);

            }, q, result);

            tasks::RunSome();
            REQUIRE(*result->value == 42);
        }

        SECTION("Save result (lvalue awaiter)") {
            InlineDiscardQueue<A, true, 2> q;
            etl::optional<A> result;

            auto t1 = tasks::Spawn([](InlineDiscardQueue<A, true, 2> &q,
                                     etl::optional<A> &result) -> Task<> {

                REQUIRE(!result);
                q.Push(42);
                while (co_await tasks::Switch());
                REQUIRE(*result->value == 42);

            }, q, result);

            auto t2 = tasks::Spawn([](InlineDiscardQueue<A, true, 2> &q,
                                     etl::optional<A> &result) -> Task<> {
                auto awaiter = q.Pop();
                co_await tasks::SaveResult(awaiter, result);
                REQUIRE(*result->value == 42);

            }, q, result);

            tasks::RunSome();
            REQUIRE(*result->value == 42);
        }

        SECTION("Basic head drop") {
            InlineDiscardQueue<A, false, 2> q;

            auto t1 = tasks::Spawn([](InlineDiscardQueue<A, false, 2> &q) -> Task<> {
                REQUIRE(results.empty());
                results.push_back("T1:1");

                REQUIRE(q.Push(1));
                REQUIRE(q.Push(2));
                REQUIRE(!q.Push(3));

                while (co_await tasks::Switch()) {}

                CheckResult(4, "T2:3");
                results.push_back("T1:2");
                REQUIRE(q.Push(4));
            }, q);

            auto t2 = tasks::Spawn([](InlineDiscardQueue<A, false, 2> &q) -> Task<> {
                CheckResult(1, "T1:1");
                results.push_back("T2:1");
                {
                    A a = co_await q.Pop();
                    REQUIRE(*a.value == 2);
                }

                CheckResult(2, "T2:1");
                results.push_back("T2:2");
                {
                    A a = co_await q.Pop();
                    REQUIRE(*a.value == 3);
                }

                CheckResult(3, "T2:2");
                results.push_back("T2:3");
                {
                    A a = co_await q.Pop();
                    REQUIRE(*a.value == 4);
                }

                CheckResult(5, "T1:2");
                results.push_back("T2:4");
            }, q);

            tasks::RunSome();

            CheckResult(6, "T2:4");
        }

        SECTION("Awaiter destruction") {
            InlineDiscardQueue<A, true, 2> q;

            auto t1 = tasks::Spawn([](InlineDiscardQueue<A, true, 2> &q) -> Task<> {
                q.Pop();
                q.Pop();
                co_return;
            }, q);

            tasks::RunSome();

            REQUIRE(t1.IsFinished());
        }

        SECTION("Awaited Awaiter destruction") {
            InlineDiscardQueue<A, true, 2> q;
            TokenQueue<> tq;

            auto t1 = tasks::Spawn([](InlineDiscardQueue<A, true, 2> &q,
                                     TokenQueue<> &tq) -> Task<> {
                size_t idx = co_await tasks::WhenAny(tq.Take(), q.Pop());
                REQUIRE(idx == 0);
            }, q, tq);

            auto t2 = tasks::Spawn([](TokenQueue<> &tq) -> Task<> {
                tq.Push();
                // Task::Make wrapper, handler task in AnyTaskAwaiter, t1
                while (co_await tasks::Switch()) {}
            }, tq);

            tasks::RunSome();

            REQUIRE(t1.IsFinished());
            REQUIRE(t2.IsFinished());
        }

        SECTION("Queue destruction (pop)") {
            std::optional<InlineDiscardQueue<A, true, 2>> q;
            TokenQueue<> tq;

            q.emplace();

            auto t1 = tasks::Spawn([](std::optional<InlineDiscardQueue<A, true, 2>> &q,
                                     TokenQueue<> &tq) -> Task<> {
                std::optional<A> result;
                auto getResult = [](std::optional<InlineDiscardQueue<A, true, 2>> &q,
                                    std::optional<A> &result) -> Awaitable<void> {
                    result.emplace(co_await q->Pop());
                };
                auto awaitable = getResult(q, result);
                REQUIRE(!result);
                size_t idx = co_await tasks::WhenAny(tq.Take(), awaitable);
                REQUIRE(idx == 1);
                REQUIRE(result);
                // Empty value should be returned
                REQUIRE(!result->value);
            }, q, tq);

            auto t2 = tasks::Spawn([](std::optional<InlineDiscardQueue<A, true, 2>> &q) -> Task<> {
                q.reset();
                co_return;
            }, q);

            tasks::RunSome();

            REQUIRE(t1.IsFinished());
            REQUIRE(t2.IsFinished());
        }
    }

    CheckStats();
}

TEST_CASE("Discard queue stress test")
{
    ResetStats();
    results.clear();

    uint32_t seed = std::random_device()();
    INFO("Random seed: " << seed);
    std::mt19937 rng(seed);

    using Queue = InlineDiscardQueue<A, true, 2>;

    Queue q1, q2, q3;

    auto t1 = tasks::Spawn([](Queue &q1, Queue &q2, Queue &q3) -> Task<> {
        while (true) {
            A r1 = 0, r2 = 0, r3 = 0;
            size_t idx = co_await tasks::WhenAny(tasks::SaveResult(q1, r1),
                                                 tasks::SaveResult(q2, r2),
                                                 tasks::SaveResult(q3, r3));
            A *r;
            switch(idx) {
            case 0:
                r = &r1;
                break;
            case 1:
                r = &r2;
                break;
            case 2:
                r = &r3;
                break;
            default:
                FAIL("Bad index");
            }
            if (*r->value == -1) {
                co_return;
            }
            REQUIRE(*r->value == static_cast<int>(idx + 1));
        }
    }, q1, q2, q3);

    auto t2 = tasks::Spawn([](Queue &q1, Queue &q2, Queue &q3, std::mt19937 &rng) -> Task<> {
        std::uniform_int_distribution<int> qDist{0, 2};
        std::uniform_int_distribution<int> swDist{0, 4};
        for (int i = 0; i < 10000; i++) {
            int qIdx = qDist(rng);
            switch (qIdx) {
            case 0:
                q1.Push(1);
                break;
            case 1:
                q2.Push(2);
                break;
            case 2:
                q3.Push(3);
                break;
            }

            int s = swDist(rng);
            if (s == 0) {
                co_await tasks::Switch();
            } else if (s == 1) {
                while (co_await tasks::Switch());
            }
        }

        while (co_await tasks::Switch());
        q1.Push(-1);
        q2.Push(-1);
        q3.Push(-1);
    }, q1, q2, q3, rng);

    tasks::RunSome();

    REQUIRE(t1.IsFinished());
    REQUIRE(t2.IsFinished());

    CheckStats();
}


TEST_CASE("Discard queue stress test (interrupts)")
{
    ResetStats();
    results.clear();

    uint32_t seed = std::random_device()();
    INFO("Random seed: " << seed);
    std::mt19937 rng(seed);

    using Queue = InlineDiscardQueue<A, true, 2>;

    Queue q1, q2, q3;
    Timer t;

    auto t1 = tasks::Spawn([](Queue &q1, Queue &q2, Queue &q3, Timer &t) -> Task<> {
        t.ExpiresAfter(2);
        while (true) {
            A r1 = 0, r2 = 0, r3 = 0;
            size_t idx = co_await tasks::WhenAny(tasks::SaveResult(q1, r1),
                                                 tasks::SaveResult(q2, r2),
                                                 tasks::SaveResult(q3, r3),
                                                 t);
            A *r;
            switch(idx) {
            case 0:
                r = &r1;
                break;
            case 1:
                r = &r2;
                break;
            case 2:
                r = &r3;
                break;
            case 3:
                t.ExpiresAfter(2);
                continue;
            default:
                FAIL("Bad index");
            }
            if (*r->value == -1) {
                t.Cancel();
                co_return;
            }
            REQUIRE(*r->value == static_cast<int>(idx + 1));
        }
    }, q1, q2, q3, t);

    Scheduler scheduler;

    std::thread t2([&](){
        std::uniform_int_distribution<int> qDist{0, 2};
        std::uniform_int_distribution<int> swDist{0, 4};
        for (int i = 0; i < 10000; i++) {
            IsrGuard g;
            Timer::Tick();
            int qIdx = qDist(rng);
            switch (qIdx) {
            case 0:
                q1.Push(1);
                break;
            case 1:
                q2.Push(2);
                break;
            case 2:
                q3.Push(3);
                break;
            }
            g.Exit();
            scheduler.Wakeup();
            if (swDist(rng) == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        IsrGuard g;
        q1.Push(-1);
        q2.Push(-1);
        q3.Push(-1);
        scheduler.Terminate();
    });

    scheduler.Run();

    REQUIRE(t1.IsFinished());
    t2.join();

    CheckStats();
}
