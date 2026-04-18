#include <catch2/catch_test_macros.hpp>
#include <pulse/blocking_queue.h>

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


TEST_CASE("Blocking queue", "[single]")
{
    ResetStats();
    results.clear();

    {
        InlineBlockingQueue<A, 2> q;

        SECTION("Basic") {
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
    }

    CheckStats();
}
