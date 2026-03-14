#include <catch2/catch_test_macros.hpp>
#include <pulse/task.h>
#include <iostream>


using namespace pulse;


namespace {

TaskV
TestTask()
{
    co_return;
}

TTask<int>
IntTask()
{
    co_return 42;
}

} // anonymous namespace

TEST_CASE("Basic")
{
    auto t1 = Task::Spawn(TestTask(), 1);
    std::cout << "Task 1: " << t1 << "\n";

    auto t2 = Task::Spawn(IntTask(), 1);
    std::cout << "Task 2: " << t2 << "\n";

    Task::RunSome();
}
