#include <catch2/catch_test_macros.hpp>
#include <pulse/token_queue.h>

using namespace pulse;


TEST_CASE("Basic token queue")
{
    TokenQueue<> q(1, 10);

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


TEST_CASE("Token queue overflow")
{
    TokenQueue<> q(2);

    auto Task1 = [&]() -> TTask<int> {
        co_return co_await q;
    };

    auto Task2 = [&]() -> TTask<int> {
        co_return co_await q;
    };

    auto Task3 = [&]() -> TaskV {
        q.Push();
        q.Push();
        q.Push(5);
        auto t1 = Task::Spawn(Task1());
        auto t2 = Task::Spawn(Task2());

        REQUIRE(co_await t1 == 5);
        REQUIRE(co_await t2 == 6);
    };

    auto t3 = Task::Spawn(Task3());

    Task::RunSome();

    REQUIRE(t3.IsFinished());
}


TEST_CASE("WhenAny with TokenQueue")
{
    TokenQueue<> q1, q2;

    auto Task1 = [&]() -> TTask<int> {
        co_return co_await Task::WhenAny(q1, q2);
        // q2 awaiter is never resumed. It should not leak which should be visible by Valgrind.
    };

    auto Task2 = [&](TTask<int> t1) -> TaskV {
        q2.Push();
        REQUIRE(co_await t1 == 1);
    };

    auto t1 = Task::Spawn(Task1());
    auto t2 = Task::Spawn(Task2(t1));

    Task::RunSome();

    REQUIRE(t2.IsFinished());
}
