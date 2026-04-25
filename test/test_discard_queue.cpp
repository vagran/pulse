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

            auto t1 = Task::Spawn([](InlineDiscardQueue<A, true, 2> &q) -> TaskV {
                REQUIRE(results.empty());
                results.push_back("T1:1");

                REQUIRE(q.Push(1));
                REQUIRE(q.Push(2));
                REQUIRE(!q.Push(3));

                while (co_await Task::Switch()) {}

                CheckResult(4, "T2:3");
                results.push_back("T1:2");
                REQUIRE(q.Push(4));
            }, q);

            auto t2 = Task::Spawn([](InlineDiscardQueue<A, true, 2> &q) -> TaskV {
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

            Task::RunSome();

            CheckResult(6, "T2:4");
        }

        SECTION("Basic head drop") {
            InlineDiscardQueue<A, false, 2> q;

            auto t1 = Task::Spawn([](InlineDiscardQueue<A, false, 2> &q) -> TaskV {
                REQUIRE(results.empty());
                results.push_back("T1:1");

                REQUIRE(q.Push(1));
                REQUIRE(q.Push(2));
                REQUIRE(!q.Push(3));

                while (co_await Task::Switch()) {}

                CheckResult(4, "T2:3");
                results.push_back("T1:2");
                REQUIRE(q.Push(4));
            }, q);

            auto t2 = Task::Spawn([](InlineDiscardQueue<A, false, 2> &q) -> TaskV {
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

            Task::RunSome();

            CheckResult(6, "T2:4");
        }

        SECTION("Awaiter destruction") {
            InlineDiscardQueue<A, true, 2> q;

            auto t1 = Task::Spawn([](InlineDiscardQueue<A, true, 2> &q) -> TaskV {
                q.Pop();
                q.Pop();
                co_return;
            }, q);

            Task::RunSome();

            REQUIRE(t1.IsFinished());
        }

        SECTION("Awaited Awaiter destruction") {
            InlineDiscardQueue<A, true, 2> q;
            TokenQueue<> tq;

            auto t1 = Task::Spawn([](InlineDiscardQueue<A, true, 2> &q,
                                     TokenQueue<> &tq) -> TaskV {
                size_t idx = co_await Task::WhenAny(tq.Take(), q.Pop());
                REQUIRE(idx == 0);
            }, q, tq);

            auto t2 = Task::Spawn([](TokenQueue<> &tq) -> TaskV {
                tq.Push();
                // Task::Make wrapper, handler task in AnyTaskAwaiter, t1
                while (co_await Task::Switch()) {}
            }, tq);

            Task::RunSome();

            REQUIRE(t1.IsFinished());
            REQUIRE(t2.IsFinished());
        }

        SECTION("Queue destruction (pop)") {
            std::optional<InlineDiscardQueue<A, true, 2>> q;
            TokenQueue<> tq;

            q.emplace();

            auto t1 = Task::Spawn([](std::optional<InlineDiscardQueue<A, true, 2>> &q,
                                     TokenQueue<> &tq) -> TaskV {
                std::optional<A> result;
                auto getResult = [](std::optional<InlineDiscardQueue<A, true, 2>> &q,
                                    std::optional<A> &result) -> Awaitable<void> {
                    result.emplace(co_await q->Pop());
                };
                auto awaitable = getResult(q, result);
                REQUIRE(!result);
                size_t idx = co_await Task::WhenAny(tq.Take(), awaitable);
                REQUIRE(idx == 1);
                REQUIRE(result);
                // Empty value should be returned
                REQUIRE(!result->value);
            }, q, tq);

            auto t2 = Task::Spawn([](std::optional<InlineDiscardQueue<A, true, 2>> &q) -> TaskV {
                q.reset();
                co_return;
            }, q);

            Task::RunSome();

            REQUIRE(t1.IsFinished());
            REQUIRE(t2.IsFinished());
        }
    }

    CheckStats();
}
