#include <catch2/catch_test_macros.hpp>
#include <pulse/blocking_queue.h>
#include <pulse/token_queue.h>


using namespace pulse;

namespace {

size_t numConstructed = 0, numDestructed = 0, numValuesConstructed = 0, numValuesDestructed = 0;

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
    REQUIRE(numConstructed == numDestructed);
    REQUIRE(numValuesConstructed == numValuesDestructed);
}

class A {
public:
    // Use dynamic allocation to help troubleshooting with Valgrind.
    std::unique_ptr<int> value;

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

} // anonymous namespace


TEST_CASE("Blocking queue")
{
    ResetStats();
    results.clear();

    {
        SECTION("Basic") {
            InlineBlockingQueue<A, 2> q;

            auto t1 = Task::Spawn([&]() -> TaskV {
                REQUIRE(results.empty());
                results.push_back("T1:1");
                co_await q.Push(1);

                CheckResult(1, "T1:1");
                results.push_back("T1:2");
                co_await q.Push(2);

                CheckResult(2, "T1:2");
                results.push_back("T1:3");
                co_await q.Push(3); // switch

                CheckResult(7, "T2:4");
                results.push_back("T1:4");
                // Thiss one goes into pending awaiter
                REQUIRE(q.TryPush(4));

                CheckResult(8, "T1:4");
                results.push_back("T1:5");
                REQUIRE(q.TryEmplace(5));

                CheckResult(9, "T1:5");
                results.push_back("T1:6");
                co_await q.Emplace(6);

                CheckResult(10, "T1:6");
                results.push_back("T1:7");
            });

            auto t2 = Task::Spawn([&]() -> TaskV {
                CheckResult(3, "T1:3");
                results.push_back("T2:1");
                REQUIRE(*(co_await q.Pop()).value == 1);

                CheckResult(4, "T2:1");
                results.push_back("T2:2");
                REQUIRE(*(co_await q.Pop()).value == 2);

                CheckResult(5, "T2:2");
                results.push_back("T2:3");
                // This one is taken from pending awaiter
                REQUIRE(*(co_await q.Pop()).value == 3);

                CheckResult(6, "T2:3");
                results.push_back("T2:4");
                REQUIRE(*(co_await q.Pop()).value == 4); // switch

                CheckResult(11, "T1:7");
                results.push_back("T2:5");
                REQUIRE(*(co_await q.Pop()).value == 5);

                CheckResult(12, "T2:5");
                results.push_back("T2:6");
                REQUIRE(*(co_await q.Pop()).value == 6);

                CheckResult(13, "T2:6");
                results.push_back("T2:7");
            });

            Task::RunSome();

            CheckResult(14, "T2:7");
        }

        SECTION("Awaiter destruction") {
            InlineBlockingQueue<A, 2> q;

            auto t1 = Task::Spawn([&]() -> TaskV {
                q.Push(1);
                q.Push(2);
                q.Push(3);
                q.Push(4);
                co_return;
            });

            auto t2 = Task::Spawn([&]() -> TaskV {
                REQUIRE(*(co_await q.Pop()).value == 1);
                REQUIRE(*(co_await q.Pop()).value == 2);
                REQUIRE(!q.TryPop());
            });

            Task::RunSome();

            REQUIRE(t1.IsFinished());
            REQUIRE(t2.IsFinished());
        }

        SECTION("Awaited Awaiter destruction") {
            InlineBlockingQueue<A, 2> q;
            TokenQueue<> tq;

            auto t1 = Task::Spawn([&]() -> TaskV {
                REQUIRE(q.TryPush(1));
                REQUIRE(q.TryPush(2));
                REQUIRE(co_await Task::WhenAny(tq.Take(), q.Push(3)) == 0);
            });

            auto t2 = Task::Spawn([&]() -> TaskV {
                tq.Push();
                // Task::Make wrapper, handler task in AnyTaskAwaiter, t1
                while (co_await Task::Switch());
                REQUIRE(*(co_await q.Pop()).value == 1);
                REQUIRE(*(co_await q.Pop()).value == 2);
                REQUIRE(!q.TryPop());
            });

            Task::RunSome();

            REQUIRE(t1.IsFinished());
            REQUIRE(t2.IsFinished());
        }

        SECTION("Queue destruction") {
            std::optional<InlineBlockingQueue<A, 2>> q;
            TokenQueue<> tq;

            q.emplace();

            auto t1 = Task::Spawn([&]() -> TaskV {
                REQUIRE(q->TryPush(1));
                REQUIRE(q->TryPush(2));
                REQUIRE(co_await Task::WhenAny(tq.Take(), q->Push(3)) == 1);
            });

            auto t2 = Task::Spawn([&]() -> TaskV {
                q.reset();
                co_return;
            });

            Task::RunSome();

            REQUIRE(t1.IsFinished());
            REQUIRE(t2.IsFinished());
        }

        SECTION("Queue destruction (pop)") {
            std::optional<InlineBlockingQueue<A, 2>> q;
            TokenQueue<> tq;

            q.emplace();

            auto t1 = Task::Spawn([&]() -> TaskV {
                std::optional<A> result;
                auto getResult = [&]() -> Awaitable<void> {
                    result.emplace(co_await q->Pop());
                };
                auto awaitable = getResult();
                REQUIRE(!result);
                REQUIRE(co_await Task::WhenAny(tq.Take(), awaitable) == 1);
                REQUIRE(result);
                // Empty value should be returned
                REQUIRE(!result->value);
            });

            auto t2 = Task::Spawn([&]() -> TaskV {
                q.reset();
                co_return;
            });

            Task::RunSome();

            REQUIRE(t1.IsFinished());
            REQUIRE(t2.IsFinished());
        }
    }

    CheckStats();
}
