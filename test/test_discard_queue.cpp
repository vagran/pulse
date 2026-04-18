#include <catch2/catch_test_macros.hpp>
#include <pulse/discard_queue.h>
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


TEST_CASE("Discard queue")
{
    ResetStats();
    results.clear();

    {
        SECTION("Basic") {
            InlineDiscardQueue<A, true, 2> q;

            auto t1 = Task::Spawn([&]() -> TaskV {
                REQUIRE(results.empty());
                results.push_back("T1:1");

                REQUIRE(q.Push(1));
                REQUIRE(q.Push(2));
                REQUIRE(!q.Push(3));

                while (co_await  Task::Switch());

                CheckResult(4, "T2:3");
                results.push_back("T1:2");
                REQUIRE(q.Push(4));
            });

            auto t2 = Task::Spawn([&]() -> TaskV {
                CheckResult(1, "T1:1");
                results.push_back("T2:1");
                REQUIRE(*(co_await q.Pop()).value == 1);

                CheckResult(2, "T2:1");
                results.push_back("T2:2");
                REQUIRE(*(co_await q.Pop()).value == 2);

                CheckResult(3, "T2:2");
                results.push_back("T2:3");
                REQUIRE(*(co_await q.Pop()).value == 4);

                CheckResult(5, "T1:2");
                results.push_back("T2:4");
            });

            Task::RunSome();

            CheckResult(6, "T2:4");
        }

        SECTION("Basic head drop") {
            InlineDiscardQueue<A, false, 2> q;

            auto t1 = Task::Spawn([&]() -> TaskV {
                REQUIRE(results.empty());
                results.push_back("T1:1");

                REQUIRE(q.Push(1));
                REQUIRE(q.Push(2));
                REQUIRE(!q.Push(3));

                while (co_await  Task::Switch());

                CheckResult(4, "T2:3");
                results.push_back("T1:2");
                REQUIRE(q.Push(4));
            });

            auto t2 = Task::Spawn([&]() -> TaskV {
                CheckResult(1, "T1:1");
                results.push_back("T2:1");
                REQUIRE(*(co_await q.Pop()).value == 2);

                CheckResult(2, "T2:1");
                results.push_back("T2:2");
                REQUIRE(*(co_await q.Pop()).value == 3);

                CheckResult(3, "T2:2");
                results.push_back("T2:3");
                REQUIRE(*(co_await q.Pop()).value == 4);

                CheckResult(5, "T1:2");
                results.push_back("T2:4");
            });

            Task::RunSome();

            CheckResult(6, "T2:4");
        }

        SECTION("Awaiter destruction") {
            InlineDiscardQueue<A, true, 2> q;

            auto t1 = Task::Spawn([&]() -> TaskV {
                q.Pop();
                q.Pop();
                co_return;
            });

            Task::RunSome();

            REQUIRE(t1.IsFinished());
        }

        SECTION("Awaited Awaiter destruction") {
            InlineDiscardQueue<A, true, 2> q;
            TokenQueue<> tq;

            auto t1 = Task::Spawn([&]() -> TaskV {
                REQUIRE(co_await Task::WhenAny(tq.Take(), q.Pop()) == 0);
            });

            auto t2 = Task::Spawn([&]() -> TaskV {
                tq.Push();
                // Task::Make wrapper, handler task in AnyTaskAwaiter, t1
                while (co_await Task::Switch());
            });

            Task::RunSome();

            REQUIRE(t1.IsFinished());
            REQUIRE(t2.IsFinished());
        }

        SECTION("Queue destruction (pop)") {
            std::optional<InlineDiscardQueue<A, true, 2>> q;
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
