#include <catch2/catch_test_macros.hpp>
#include <pulse/token_queue.h>

using namespace pulse;


TEST_CASE("Basic")
{
    TokenQueue<> q(10);

    auto Task1 = [&]() -> TTask<int> {
        co_return co_await q;
    };

    auto Task2 = [&]() -> TTask<int> {
        co_return co_await q;
    };

    auto Task3 = [&]() -> TaskV {
        q.Push();
        q.Push();
        co_return;
    };

    auto t1 = Task::Spawn(Task1());
    auto t2 = Task::Spawn(Task2());
    auto t3 = Task::Spawn(Task3());

    Task::RunSome();

    REQUIRE(t1.GetResult() == 10);
    REQUIRE(t2.GetResult() == 11);
}
