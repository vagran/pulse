#include <catch2/catch_test_macros.hpp>
#include <pulse/task.h>
#include <pulse/token_queue.h>
#include <pulse/timer.h>
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

TEST_CASE("Basic tasks")
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


TEST_CASE("Spawn by function")
{
    struct Tasks {
        static TTask<int>
        IntTask()
        {
            co_return 42;
        }

        static TTask<int>
        IntTaskArg(int arg)
        {
            co_return 42 + arg;
        }

        static int
        IntFunc()
        {
            return 10;
        }
    };

    auto t1 = Task::Spawn(Tasks::IntTask);
    auto t2 = Task::Spawn(Tasks::IntTaskArg, 10);
    auto t3 = Task::Spawn(Tasks::IntFunc);
    auto t4 = Task::Spawn([](int a, int b){return a + b;}, 3, 5);

    Task::RunSome();

    REQUIRE(t1.GetResult() == 42);
    REQUIRE(t2.GetResult() == 52);
    REQUIRE(t3.GetResult() == 10);
    REQUIRE(t4.GetResult() == 8);
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
            REQUIRE(!co_await Task::Switch());
            CheckResult(6, "T1:1");
            results.push_back("T1:2");
        }

        static TaskV
        T2()
        {
            CheckResult(1, "T3:1");
            results.push_back("T2:1");
            REQUIRE(!co_await Task::Switch());
            // Should not suspend
            CheckResult(2, "T2:1");
            results.push_back("T2:2");
        }

        static TaskV
        T3(TaskV t2)
        {
            REQUIRE(results.empty());
            results.push_back("T3:1");
            // t2 will raise priority on this point
            co_await t2;
            CheckResult(3, "T2:2");
            results.push_back("T3:2");
            REQUIRE(!co_await Task::Switch());
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


TEST_CASE("WhenAll")
{
    struct Tasks {
        static TaskV
        T1(TokenQueue<> &q)
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static TaskV
        T2(TokenQueue<> &q)
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(5, "T1:2");
            results.push_back("T2:2");
        }

        static TaskV
        T3(Task t1, Task t2)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");
            // Check awaiter passing.
            co_await Task::WhenAll(t1, t2);
            CheckResult(6, "T2:2");
            results.push_back("T3:2");
        }

        static TaskV
        T4(TokenQueue<> &q1, TokenQueue<> &q2)
        {
            CheckResult(3, "T3:1");
            results.push_back("T4:1");
            q1.Push();
            q2.Push();
            co_return;
        }
    };

    TokenQueue<> q1, q2;

    results.clear();

    auto t1 = Task::Spawn(Tasks::T1(q1));

    auto t2 = Task::Spawn(Tasks::T2(q2));

    auto t3 = Task::Spawn(Tasks::T3(t1, t2));

    auto t4 = Task::Spawn(Tasks::T4(q1, q2));

    Task::RunSome();

    CheckResult(7, "T3:2");
}


TEST_CASE("WhenAll - mixed task/awaiter")
{
    struct Tasks {
        static TaskV
        T1(TokenQueue<> &q)
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static TaskV
        T2(TokenQueue<> &q)
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(5, "T1:2");
            results.push_back("T2:2");
        }

        static TaskV
        T3(Task t1, Task t2)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");
            // Check awaiter passing.
            co_await Task::WhenAll(t1, t2.Wait());
            CheckResult(6, "T2:2");
            results.push_back("T3:2");
        }

        static TaskV
        T4(TokenQueue<> &q1, TokenQueue<> &q2)
        {
            CheckResult(3, "T3:1");
            results.push_back("T4:1");
            q1.Push();
            q2.Push();
            co_return;
        }
    };

    TokenQueue<> q1, q2;

    results.clear();

    auto t1 = Task::Spawn(Tasks::T1(q1));

    auto t2 = Task::Spawn(Tasks::T2(q2));

    auto t3 = Task::Spawn(Tasks::T3(t1, t2));

    auto t4 = Task::Spawn(Tasks::T4(q1, q2));

    Task::RunSome();

    CheckResult(7, "T3:2");
}


TEST_CASE("WhenAll - awaiters")
{
    struct Tasks {
        static TaskV
        T1(TokenQueue<> &q)
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static TaskV
        T2(TokenQueue<> &q)
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(5, "T1:2");
            results.push_back("T2:2");
        }

        static TaskV
        T3(Task t1, Task t2)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");
            // Check awaiter passing.
            co_await Task::WhenAll(t1.Wait(), t2.Wait());
            CheckResult(6, "T2:2");
            results.push_back("T3:2");
        }

        static TaskV
        T4(TokenQueue<> &q1, TokenQueue<> &q2)
        {
            CheckResult(3, "T3:1");
            results.push_back("T4:1");
            q1.Push();
            q2.Push();
            co_return;
        }
    };

    TokenQueue<> q1, q2;

    results.clear();

    auto t1 = Task::Spawn(Tasks::T1(q1));

    auto t2 = Task::Spawn(Tasks::T2(q2));

    auto t3 = Task::Spawn(Tasks::T3(t1, t2));

    auto t4 = Task::Spawn(Tasks::T4(q1, q2));

    Task::RunSome();

    CheckResult(7, "T3:2");
}


TEST_CASE("WhenAll - completed")
{
    struct Tasks {
        static TaskV
        T1()
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_return;
        }

        static TaskV
        T2()
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_return;
        }

        static TaskV
        T3(Task t1, Task t2)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");
            co_await Task::WhenAll(t1, t2.Wait());
            results.push_back("T3:2");
        }
    };

    results.clear();

    auto t1 = Task::Spawn(Tasks::T1());

    auto t2 = Task::Spawn(Tasks::T2());

    auto t3 = Task::Spawn(Tasks::T3(t1, t2));

    Task::RunSome();

    CheckResult(4, "T3:2");
}


TEST_CASE("WhenAll - priority")
{
    struct Tasks {
        static TaskV
        T1(TokenQueue<> &q)
        {
            CheckResult(1, "T3:1");
            results.push_back("T1:1");
            co_await q;
            CheckResult(5, "T5:1");
            results.push_back("T1:2");
        }

        static TaskV
        T2(TokenQueue<> &q)
        {
            CheckResult(2, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(7, "T4:2");
            results.push_back("T2:2");
        }

        static TaskV
        T3(Task t1, Task t2)
        {
            REQUIRE(results.empty());
            results.push_back("T3:1");
            // Cannot use t.Wait() here since priority is not propagated to awaitable wrapper task.
            co_await Task::WhenAll(t1, t2);
            CheckResult(8, "T2:2");
            results.push_back("T3:2");
        }

        static TaskV
        T4(TokenQueue<> &q1, TokenQueue<> &q2, TokenQueue<> &q5)
        {
            CheckResult(3, "T2:1");
            results.push_back("T4:1");
            q1.Push();
            co_await Task::Switch();
            CheckResult(6, "T1:2");
            results.push_back("T4:2");
            q2.Push();
            q5.Push();
            co_return;
        }

        static TaskV
        T5(TokenQueue<> &q)
        {
            CheckResult(4, "T4:1");
            results.push_back("T5:1");
            co_await q;
            CheckResult(9, "T3:2");
            results.push_back("T5:2");
        }
    };

    TokenQueue<> q1, q2, q5;

    results.clear();

    auto t1 = Task::Spawn(Tasks::T1(q1));

    auto t2 = Task::Spawn(Tasks::T2(q2));

    auto t3 = Task::Spawn(Tasks::T3(t1, t2), 0);

    auto t4 = Task::Spawn(Tasks::T4(q1, q2, q5));

    auto t5 = Task::Spawn(Tasks::T5(q5));

    Task::RunSome();

    CheckResult(10, "T5:2");
}


TEST_CASE("WhenAny")
{
    struct Tasks {
        static TaskV
        T1(TokenQueue<> &q)
        {
            CheckResult(1, "T3:1");
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static TaskV
        T2(TokenQueue<> &q)
        {
            CheckResult(2, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(7, "T4:2");
            results.push_back("T2:2");
        }

        static TaskV
        T3(Task t1, Task t2)
        {
            REQUIRE(results.empty());
            results.push_back("T3:1");
            // Check awaiter passing.
            co_await Task::WhenAny(t1, t2);
            CheckResult(5, "T1:2");
            results.push_back("T3:2");
        }

        static TaskV
        T4(TokenQueue<> &q1, TokenQueue<> &q2)
        {
            CheckResult(3, "T2:1");
            results.push_back("T4:1");
            q1.Push();
            co_await Task::Switch();
            CheckResult(6, "T3:2");
            results.push_back("T4:2");
            q2.Push();
            co_return;
        }
    };

    TokenQueue<> q1, q2;

    results.clear();

    auto t1 = Task::Spawn(Tasks::T1(q1));

    auto t2 = Task::Spawn(Tasks::T2(q2));

    auto t3 = Task::Spawn(Tasks::T3(t1, t2), 0);

    auto t4 = Task::Spawn(Tasks::T4(q1, q2));

    Task::RunSome();

    CheckResult(8, "T2:2");
}


TEST_CASE("WhenAny - mixed task/awaiters")
{
    struct Tasks {
        static TaskV
        T1(TokenQueue<> &q)
        {
            CheckResult(1, "T3:1");
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static TaskV
        T2(TokenQueue<> &q)
        {
            CheckResult(2, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(7, "T4:2");
            results.push_back("T2:2");
        }

        static TaskV
        T3(Task t1, Task t2)
        {
            REQUIRE(results.empty());
            results.push_back("T3:1");
            // Check awaiter passing.
            co_await Task::WhenAny(t1, t2.Wait());
            CheckResult(5, "T1:2");
            results.push_back("T3:2");
        }

        static TaskV
        T4(TokenQueue<> &q1, TokenQueue<> &q2)
        {
            CheckResult(3, "T2:1");
            results.push_back("T4:1");
            q1.Push();
            co_await Task::Switch();
            CheckResult(6, "T3:2");
            results.push_back("T4:2");
            q2.Push();
            co_return;
        }
    };

    TokenQueue<> q1, q2;

    results.clear();

    auto t1 = Task::Spawn(Tasks::T1(q1));

    auto t2 = Task::Spawn(Tasks::T2(q2));

    auto t3 = Task::Spawn(Tasks::T3(t1, t2), 0);

    auto t4 = Task::Spawn(Tasks::T4(q1, q2));

    Task::RunSome();

    CheckResult(8, "T2:2");
}


TEST_CASE("WhenAny - awaiters")
{
    struct Tasks {
        static TaskV
        T1(TokenQueue<> &q)
        {
            CheckResult(1, "T3:1");
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static TaskV
        T2(TokenQueue<> &q)
        {
            CheckResult(2, "T1:1");
            results.push_back("T2:1");
            co_await q;
            // should not be reached
            FAIL();
        }

        static TaskV
        T3(Task t1, Task t2)
        {
            REQUIRE(results.empty());
            results.push_back("T3:1");
            // Check awaiter passing.
            co_await Task::WhenAny(t1.Wait(), t2.Wait());
            CheckResult(5, "T1:2");
            results.push_back("T3:2");
        }

        static TaskV
        T4(TokenQueue<> &q1)
        {
            CheckResult(3, "T2:1");
            results.push_back("T4:1");
            q1.Push();
            co_return;
        }
    };

    TokenQueue<> q1, q2;

    results.clear();

    auto t1 = Task::Spawn(Tasks::T1(q1));

    auto t2 = Task::Spawn(Tasks::T2(q2));

    auto t3 = Task::Spawn(Tasks::T3(t1, t2), 0);

    auto t4 = Task::Spawn(Tasks::T4(q1));

    Task::RunSome();

    CheckResult(6, "T3:2");
}


TEST_CASE("WhenAny - completed")
{
    struct Tasks {
        static TaskV
        T1()
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_return;
        }

        static TaskV
        T2(TokenQueue<> &q)
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(4, "T3:2");
            results.push_back("T2:2");
        }

        static TaskV
        T3(Task t1, Task t2, TokenQueue<> &q)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");
            co_await Task::WhenAny(t1, t2.Wait());
            CheckResult(3, "T3:1");
            results.push_back("T3:2");
            q.Push();
        }
    };

    TokenQueue<> q;

    results.clear();

    auto t1 = Task::Spawn(Tasks::T1());

    auto t2 = Task::Spawn(Tasks::T2(q));

    auto t3 = Task::Spawn(Tasks::T3(t1, t2, q));

    Task::RunSome();

    CheckResult(5, "T2:2");
}


TEST_CASE("WhenAny - stress")
{
    struct Tasks {
        static TaskV
        T1(TokenQueue<> &q)
        {
            Timer timer;

            while (true) {
                timer.ExpiresAfter(10);
                size_t idx = co_await Task::WhenAny(q, timer);
                REQUIRE(idx == 0);
                if (q.Peek() >= 1000) {
                    break;
                }
            }

            timer.Cancel();
        }

        static TaskV
        T2(TokenQueue<> &q)
        {
            for (int i = 0; i < 1010; i++) {
                q.Push();
                co_await Task::Switch();
            }
        }
    };

    TokenQueue<> q;

    auto t1 = Task::Spawn(Tasks::T1(q));

    auto t2 = Task::Spawn(Tasks::T2(q));

    Task::RunSome();

    REQUIRE(t1.IsFinished());
    REQUIRE(t2.IsFinished());
}
