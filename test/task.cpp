#include <catch2/catch_test_macros.hpp>
#include <pulse/task.h>
#include <iostream>


using namespace pulse;


namespace {

Task
TestTask()
{
    co_return;
}

} // anonymous namespace

TEST_CASE("Basic")
{
    auto t1 = Task::Spawn(TestTask(), 1);
    std::cout << "Task 1: " << t1 << "\n";
}
