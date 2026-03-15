#include <catch2/catch_test_macros.hpp>
#include <pulse/task.h>
#include <iostream>


using namespace pulse;


namespace {

std::vector<std::string> results;

TaskV
TestTask()
{
    REQUIRE(results.empty());
    results.push_back("T1:1");
    co_await Task::Switch();

    REQUIRE(results.size() == 3);
    REQUIRE(results.back() == "T3:1");
    results.push_back("T1:2");

    co_return;
}

TTask<int>
IntTask()
{
    REQUIRE(results.size() == 1);
    REQUIRE(results.back() == "T1:1");
    results.push_back("T2:1");
    co_await Task::Switch();

    co_return 42;
}

TTask<std::string>
StrTask()
{
    REQUIRE(results.size() == 2);
    REQUIRE(results.back() == "T2:1");
    results.push_back("T3:1");
    co_await Task::Switch();

    co_return "result";
}

} // anonymous namespace

TEST_CASE("Basic")
{
    auto t1 = Task::Spawn(TestTask(), 1);

    auto t2 = Task::Spawn(IntTask(), 1);

    auto t3 = Task::Spawn(StrTask(), 1);

    Task::RunSome();

    REQUIRE(results.size() == 4);
    REQUIRE(results.back() == "T1:2");
}
