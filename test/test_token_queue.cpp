#include <catch2/catch_test_macros.hpp>
#include <pulse/token_queue.h>

using namespace pulse;


TEST_CASE("Basic token queue")
{
    TokenQueue<> q(1, 10);

    auto Task1 = [](TokenQueue<> &q) -> TTask<int> {
        co_return co_await q;
    };

    auto Task2 = [](TokenQueue<> &q) -> TTask<int> {
        co_return co_await q;
    };

    auto Task3 = [](TokenQueue<> &q) -> TaskV {
        q.Push();
        q.Push();
        co_return;
    };

    auto t1 = Task::Spawn(Task1(q));
    auto t2 = Task::Spawn(Task2(q));
    auto t3 = Task::Spawn(Task3(q));

    Task::RunSome();

    REQUIRE(t1.GetResult() == 10);
    REQUIRE(t2.GetResult() == 11);
}


TEST_CASE("Token queue overflow")
{
    TokenQueue<> q(2);

    auto Task1 = [](TokenQueue<> &q) -> TTask<int> {
        co_return co_await q;
    };

    auto Task2 = [](TokenQueue<> &q) -> TTask<int> {
        co_return co_await q;
    };

    auto Task3 = [](TokenQueue<> &q, auto Task1, auto Task2) -> TaskV {
        q.Push();
        q.Push();
        q.Push(5);
        auto t1 = Task::Spawn(Task1(q));
        auto t2 = Task::Spawn(Task2(q));

        int v1 = co_await t1;
        REQUIRE(v1 == 5);
        int v2 = co_await t2;
        REQUIRE(v2 == 6);
    };

    auto t3 = Task::Spawn(Task3(q, Task1, Task2));

    Task::RunSome();

    REQUIRE(t3.IsFinished());
}


TEST_CASE("WhenAny with TokenQueue (never resumed)")
{
    TokenQueue<> q1, q2;

    auto Task1 = [](TokenQueue<> &q1, TokenQueue<> &q2) -> TTask<int> {
        co_return co_await Task::WhenAny(q1, q2);
        // q2 awaiter is never resumed. It should not leak which should be visible by Valgrind.
    };

    auto Task2 = [](TokenQueue<> &q2, TTask<int> t1) -> TaskV {
        q2.Push();
        int v = co_await t1;
        REQUIRE(v == 1);
    };

    auto t1 = Task::Spawn(Task1(q1, q2));
    auto t2 = Task::Spawn(Task2(q2, t1));

    Task::RunSome();

    REQUIRE(t2.IsFinished());
}


TEST_CASE("WhenAny with TokenQueue")
{
    TokenQueue<> q1(1, 10), q2(1, 20);

    auto Task1 = [](TokenQueue<> &q1, TokenQueue<> &q2) -> TTask<int> {
        co_return co_await Task::WhenAny(q1, q2);
    };

    auto Task2 = [](TokenQueue<> &q1, TokenQueue<> &q2, TTask<int> t1) -> TaskV {
        q2.Push();
        int v = co_await t1;
        REQUIRE(v == 1);
        q2.Push();
        v = co_await q2;
        REQUIRE(v == 21);

        // Awaiter on q1 should be removed at this point, so token should be queued and then
        // immediately returned to co_await.
        q1.Push();
        v = co_await q1;
        REQUIRE(v == 10);
    };

    auto t1 = Task::Spawn(Task1(q1, q2));
    auto t2 = Task::Spawn(Task2(q1, q2, t1));

    Task::RunSome();

    REQUIRE(t2.IsFinished());
}
