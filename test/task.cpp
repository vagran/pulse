#include <catch2/catch_test_macros.hpp>
#include <pulse/task.h>
#include <iostream>


using namespace pulse;


namespace {

std::vector<std::string> results;

void
CheckResult(size_t expectedSize, const std::string &expectedLast)
{
    REQUIRE(!results.empty());
    REQUIRE(expectedLast == results.back());
    REQUIRE(expectedSize == results.size());
}

void
CheckResult(const std::vector<std::string> &expectedResult)
{
    REQUIRE(expectedResult.size() == results.size());
    for (size_t i = 0; i < results.size(); i++) {
        REQUIRE(expectedResult[i] == results[i]);
    }
}

} // anonymous namespace

TEST_CASE("Basic")
{
    struct Tasks {

        static TaskV
        VoidTask()
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_await Task::Switch();

            CheckResult(3,  "T3:1");
            results.push_back("T1:2");

            co_return;
        }

        static TTask<int>
        IntTask()
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_await Task::Switch();

            CheckResult(4, "T1:2");
            results.push_back("T2:2");
            co_await Task::Switch();

            CheckResult(6, "T3:2");
            results.push_back("T2:3");

            co_return 42;
        }

        static TTask<std::string>
        StrTask(TTask<int> t2)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");
            co_await Task::Switch();

            CheckResult(5, "T2:2");
            results.push_back("T3:2");
            int t2ret = co_await t2;
            REQUIRE(t2ret == 42);
            CheckResult(7, "T2:3");
            results.push_back("T3:3");

            co_return "result";
        }
    };

    results.clear();

    auto t1 = Task::Spawn(Tasks::VoidTask(), 1);

    auto t2 = Task::Spawn(Tasks::IntTask(), 1);

    auto t3 = Task::Spawn(Tasks::StrTask(t2), 1);

    Task::RunSome();

    CheckResult(8, "T3:3");
    REQUIRE(t2.GetResult() == 42);
    REQUIRE(t3.GetResult() == "result");
}


TEST_CASE("Awaitable")
{
    struct Tasks {
        static Awaitable<int>
        GetInt(int i, bool first = true)
        {
            results.push_back(std::string("GetInt:") + std::to_string(i) + ":F" +
                (first ? "1" : "0"));
            co_await Task::Switch();
            if (first) {
                i = co_await GetInt(i, false);
            }
            co_return 10 + i;
        }

        static TaskV
        T1()
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            int res = co_await GetInt(5);
            REQUIRE(res == 25);
            CheckResult(6, "GetInt:7:F0");
            results.push_back("T1:2");
        }

        static TaskV
        T2()
        {
            CheckResult(2, "GetInt:5:F1");
            results.push_back("T2:1");
            int res = co_await GetInt(7);
            REQUIRE(res == 27);
            CheckResult(7, "T1:2");
            results.push_back("T2:2");
        }
    };

    results.clear();

    auto t1 = Task::Spawn(Tasks::T1(), 1);

    auto t2 = Task::Spawn(Tasks::T2(), 1);

    Task::RunSome();

    CheckResult({
        "T1:1",
        "GetInt:5:F1",
        "T2:1",
        "GetInt:7:F1",
        "GetInt:5:F0",
        "GetInt:7:F0",
        "T1:2",
        "T2:2"
    });
}


TEST_CASE("Priority propagation")
{
    struct Tasks {

        static TaskV
        T1()
        {
            CheckResult(5, "T3:3");
            results.push_back("T1:1");
            co_await Task::Switch();
            CheckResult(6, "T1:1");
            results.push_back("T1:2");
        }

        static TaskV
        T2()
        {
            CheckResult(1, "T3:1");
            results.push_back("T1:1");
            co_await Task::Switch();
            // Should not suspend
            CheckResult(2, "T1:1");
            results.push_back("T1:2");
        }

        static TaskV
        T3(TaskV t2)
        {
            REQUIRE(results.empty());
            results.push_back("T3:1");
            // t2 will raise priority on this point
            co_await t2;
            CheckResult(3, "T1:2");
            results.push_back("T3:2");
            co_await Task::Switch();
            CheckResult(4, "T3:2");
            results.push_back("T3:3");
        }
    };

    results.clear();

    auto t1 = Task::Spawn(Tasks::T1(), 1);

    auto t2 = Task::Spawn(Tasks::T2(), 1);

    auto t3 = Task::Spawn(Tasks::T3(t2), 0);

    Task::RunSome();

    CheckResult(7, "T1:2");
}
