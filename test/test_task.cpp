#include <catch2/catch_test_macros.hpp>
#include <pulse/task.h>
#include <pulse/token_queue.h>
#include <pulse/timer.h>


using namespace pulse;


namespace {

std::vector<std::string> results;

void
CheckResult(size_t expectedSize, const std::string &expectedLast)
{
    if (expectedSize != results.size()) {
        UNSCOPED_INFO("Result size mismatch: " << expectedSize << " != " << results.size());
    }
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


TEST_CASE("Unpinned task")
{
    bool called = false;

    struct Tasks {
        static Task<void>
        VoidTask(bool &called)
        {
            called = true;
            co_return;
        }
    };

    tasks::Spawn(Tasks::VoidTask(called));

    tasks::RunSome();

    REQUIRE_FALSE(called);
}


TEST_CASE("Task pin")
{
    int called = 0;

    struct Tasks {
        static Task<void>
        T1(int &called)
        {
            called++;
            co_await tasks::Switch();
            called++;
            tasks::GetCurrentTask().Unpin();
            co_await tasks::Switch();
            called++;
        }

        static Task<void>
        T2(int &called)
        {
            while (called < 2) {
                co_await tasks::Switch();
            }
        }
    };

    tasks::Spawn(Tasks::T1(called)).Pin();
    auto t2 = tasks::Spawn(Tasks::T2(called));

    tasks::RunSome();

    REQUIRE(called == 2);
    REQUIRE(t2.IsFinished());
}


TEST_CASE("Basic tasks")
{
    struct Tasks {

        static Task<void>
        VoidTask()
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_await tasks::Switch();

            CheckResult(3,  "T3:1");
            results.push_back("T1:2");
        }

        static Task<int>
        IntTask()
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_await tasks::Switch();

            CheckResult(4, "T1:2");
            results.push_back("T2:2");
            co_await tasks::Switch();

            CheckResult(6, "T3:2");
            results.push_back("T2:3");

            co_return 42;
        }

        static Task<std::string>
        StrTask(Task<int> t2)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");
            co_await tasks::Switch();

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

    auto t1 = tasks::Spawn(Tasks::VoidTask(), 1);

    auto t2 = tasks::Spawn(Tasks::IntTask(), 1);

    auto t3 = tasks::Spawn(Tasks::StrTask(t2), 1);

    tasks::RunSome();

    CheckResult(8, "T3:3");
    REQUIRE(t2.GetResult() == 42);
    REQUIRE(t3.GetResult() == "result");
}


TEST_CASE("Spawn by function")
{
    struct Tasks {
        static Task<int>
        IntTask()
        {
            co_return 42;
        }

        static Task<int>
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

    auto t1 = tasks::Spawn(Tasks::IntTask);
    auto t2 = tasks::Spawn(Tasks::IntTaskArg, 10);
    auto t3 = tasks::Spawn(Tasks::IntFunc);
    auto t4 = tasks::Spawn([](int a, int b){return a + b;}, 3, 5);

    tasks::RunSome();

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
            co_await tasks::Switch();
            if (first) {
                i = co_await GetInt(i, false);
            }
            co_return 10 + i;
        }

        static Task<>
        T1()
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            int res = co_await GetInt(5);
            REQUIRE(res == 25);
            CheckResult(6, "GetInt:7:F0");
            results.push_back("T1:2");
        }

        static Task<void>
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

    auto t1 = tasks::Spawn(Tasks::T1(), 1);

    auto t2 = tasks::Spawn(Tasks::T2(), 1);

    tasks::RunSome();

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

        static Task<>
        T1()
        {
            CheckResult(5, "T3:3");
            results.push_back("T1:1");
            bool sw = co_await tasks::Switch();
            REQUIRE(!sw);
            CheckResult(6, "T1:1");
            results.push_back("T1:2");
        }

        static Task<>
        T2()
        {
            CheckResult(1, "T3:1");
            results.push_back("T2:1");
            bool sw = co_await tasks::Switch();
            REQUIRE(!sw);
            // Should not suspend
            CheckResult(2, "T2:1");
            results.push_back("T2:2");
        }

        static Task<>
        T3(Task<> t2)
        {
            REQUIRE(results.empty());
            results.push_back("T3:1");
            // t2 will raise priority on this point
            co_await t2;
            CheckResult(3, "T2:2");
            results.push_back("T3:2");
            bool sw = co_await tasks::Switch();
            REQUIRE(!sw);
            CheckResult(4, "T3:2");
            results.push_back("T3:3");
        }
    };

    results.clear();

    auto t1 = tasks::Spawn(Tasks::T1(), 1);

    auto t2 = tasks::Spawn(Tasks::T2(), 1);

    auto t3 = tasks::Spawn(Tasks::T3(t2), 0);

    tasks::RunSome();

    CheckResult(7, "T1:2");
}


TEST_CASE("Switch yields to lower priority task")
{
    struct Tasks {
        static Task<>
        High()
        {
            REQUIRE(results.empty());
            results.push_back("H:1");
            // Only a lower-priority task is runnable. `Switch(true)` must still
            // yield to it, unlike the default `Switch()`.
            bool sw = co_await tasks::Switch(true);
            REQUIRE(sw);
            CheckResult(2, "L:1");
            results.push_back("H:2");
        }

        static Task<>
        Low()
        {
            // Runs only because `High` explicitly yielded to lower priority.
            CheckResult(1, "H:1");
            results.push_back("L:1");
            co_return;
        }
    };

    results.clear();

    auto high = tasks::Spawn(Tasks::High(), 0);
    auto low = tasks::Spawn(Tasks::Low(), 1);

    tasks::RunSome();

    CheckResult({"H:1", "L:1", "H:2"});
}


TEST_CASE("Switch does not yield to lower priority by default")
{
    struct Tasks {
        static Task<>
        High()
        {
            REQUIRE(results.empty());
            results.push_back("H:1");
            // Default `Switch()` only switches to same-or-higher priority tasks,
            // so it must not suspend while the only other task is lower priority.
            bool sw = co_await tasks::Switch();
            REQUIRE(!sw);
            CheckResult(1, "H:1");
            results.push_back("H:2");
        }

        static Task<>
        Low()
        {
            CheckResult(2, "H:2");
            results.push_back("L:1");
            co_return;
        }
    };

    results.clear();

    auto high = tasks::Spawn(Tasks::High(), 0);
    auto low = tasks::Spawn(Tasks::Low(), 1);

    tasks::RunSome();

    CheckResult({"H:1", "H:2", "L:1"});
}


TEST_CASE("Switch lower priority fairness")
{
    struct Tasks {
        static Task<>
        High(const char *name)
        {
            results.push_back(std::string(name) + ":1");
            bool sw = co_await tasks::Switch(true);
            REQUIRE(sw);
            results.push_back(std::string(name) + ":2");
        }

        static Task<>
        Low()
        {
            results.push_back("L:1");
            // At this point only the switched-out tasks remain. `Switch(true)`
            // must never switch into them, so it returns immediately.
            bool sw = co_await tasks::Switch(true);
            REQUIRE(!sw);
            results.push_back("L:2");
        }
    };

    results.clear();

    auto h1 = tasks::Spawn(Tasks::High("H1"), 0);
    auto h2 = tasks::Spawn(Tasks::High("H2"), 0);
    auto low = tasks::Spawn(Tasks::Low(), 1);

    tasks::RunSome();

    // Both high-priority tasks yield (in FIFO order), the low task runs to
    // completion, then the switched-out tasks resume in the order they yielded.
    CheckResult({
        "H1:1",
        "H2:1",
        "L:1",
        "L:2",
        "H1:2",
        "H2:2",
    });
}


TEST_CASE("Switch lower priority with priority change")
{
    struct Tasks {
        static Task<>
        Parked()
        {
            results.push_back("P:1");
            bool sw = co_await tasks::Switch(true);
            REQUIRE(sw);
            results.push_back("P:2");
        }

        static Task<>
        Raiser(TaskRef parked)
        {
            results.push_back("R:1");
            // `parked` is currently suspended in the switched-tasks queue.
            // Changing its priority must de-park it cleanly (no panic).
            parked.SetPriority(tasks::HIGHEST_PRIORITY);
            results.push_back("R:2");
            co_return;
        }

        static Task<>
        Filler()
        {
            results.push_back("F:1");
            co_return;
        }
    };

    results.clear();

    auto p = tasks::Spawn(Tasks::Parked(), 1);
    auto r = tasks::Spawn(Tasks::Raiser(p), 1);
    auto f = tasks::Spawn(Tasks::Filler(), 2);

    tasks::RunSome();

    REQUIRE(p.IsFinished());
    CheckResult({
        "P:1",
        "R:1",
        "R:2",
        "P:2",
        "F:1",
    });
}


TEST_CASE("WhenAll")
{
    struct Tasks {
        static Task<>
        T1(TokenQueue<> &q)
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static Task<>
        T2(TokenQueue<> &q)
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(5, "T1:2");
            results.push_back("T2:2");
        }

        static Task<>
        T3(TaskRef t1, TaskRef t2)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");

            auto instantTask = []() -> Awaitable<void> {
                co_return;
            };

            co_await tasks::WhenAll(t1, t2, instantTask());
            CheckResult(6, "T2:2");
            results.push_back("T3:2");
        }

        static Task<>
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

    auto t1 = tasks::Spawn(Tasks::T1(q1));

    auto t2 = tasks::Spawn(Tasks::T2(q2));

    auto t3 = tasks::Spawn(Tasks::T3(t1, t2));

    auto t4 = tasks::Spawn(Tasks::T4(q1, q2));

    tasks::RunSome();

    CheckResult(7, "T3:2");
}


TEST_CASE("WhenAll - mixed task/awaiter")
{
    struct Tasks {
        static Task<>
        T1(TokenQueue<> &q)
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static Task<>
        T2(TokenQueue<> &q)
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(5, "T1:2");
            results.push_back("T2:2");
        }

        static Task<>
        T3(TaskRef t1, TaskRef t2)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");
            // Check awaiter passing.
            co_await tasks::WhenAll(t1, t2.Wait());
            CheckResult(6, "T2:2");
            results.push_back("T3:2");
        }

        static Task<>
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

    auto t1 = tasks::Spawn(Tasks::T1(q1));

    auto t2 = tasks::Spawn(Tasks::T2(q2));

    auto t3 = tasks::Spawn(Tasks::T3(t1, t2)).Pin().GetWeakPtr();

    auto t4 = tasks::Spawn(Tasks::T4(q1, q2)).Pin().GetWeakPtr();

    tasks::RunSome();

    CheckResult(7, "T3:2");

    t3.Lock().Unpin();
    t4.Lock().Unpin();
}


TEST_CASE("WhenAll - awaiters")
{
    struct Tasks {
        static Task<>
        T1(TokenQueue<> &q)
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static Task<>
        T2(TokenQueue<> &q)
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(5, "T1:2");
            results.push_back("T2:2");
        }

        static Task<>
        T3(TaskRef t1, TaskRef t2)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");
            // Check awaiter passing.
            co_await tasks::WhenAll(t1.Wait(), t2.Wait());
            CheckResult(6, "T2:2");
            results.push_back("T3:2");
        }

        static Task<>
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

    auto t1 = tasks::Spawn(Tasks::T1(q1));

    auto t2 = tasks::Spawn(Tasks::T2(q2));

    auto t3 = tasks::Spawn(Tasks::T3(t1, t2));

    auto t4 = tasks::Spawn(Tasks::T4(q1, q2));

    tasks::RunSome();

    CheckResult(7, "T3:2");
}


TEST_CASE("WhenAll - completed mixed")
{
    struct Tasks {
        static Task<>
        T1()
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_return;
        }

        static Task<>
        T2()
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_return;
        }

        static Task<>
        T3(TaskRef t1, TaskRef t2)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");
            co_await tasks::WhenAll(t1, t2.Wait());
            results.push_back("T3:2");
        }
    };

    results.clear();

    auto t1 = tasks::Spawn(Tasks::T1());

    auto t2 = tasks::Spawn(Tasks::T2());

    auto t3 = tasks::Spawn(Tasks::T3(t1, t2));

    tasks::RunSome();

    CheckResult(4, "T3:2");
}


TEST_CASE("WhenAll - single completed")
{
    struct Tasks {
        static Task<>
        T1()
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_return;
        }

        static Task<>
        T2(TokenQueue<> &q)
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(3, "T3:1");
            results.push_back("T2:2");
            co_return;
        }

        static Task<>
        T3(TaskRef t1, TaskRef t2, TokenQueue<> &q)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");
            q.Push();
            co_await tasks::WhenAll(t1, t2);
            CheckResult(4, "T2:2");
            results.push_back("T3:2");
        }
    };

    TokenQueue<> q;

    results.clear();

    auto t1 = tasks::Spawn(Tasks::T1());

    auto t2 = tasks::Spawn(Tasks::T2(q));

    auto t3 = tasks::Spawn(Tasks::T3(t1, t2, q));

    tasks::RunSome();

    CheckResult(5, "T3:2");
}


TEST_CASE("WhenAll - completed")
{
    struct Tasks {
        static Task<>
        T1()
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_return;
        }

        static Task<>
        T2()
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_return;
        }

        static Task<>
        T3(TaskRef t1, TaskRef t2)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");
            co_await tasks::WhenAll(t1, t2);
            results.push_back("T3:2");
        }
    };

    results.clear();

    auto t1 = tasks::Spawn(Tasks::T1());

    auto t2 = tasks::Spawn(Tasks::T2());

    auto t3 = tasks::Spawn(Tasks::T3(t1, t2));

    tasks::RunSome();

    CheckResult(4, "T3:2");
}


TEST_CASE("WhenAll - priority")
{
    struct Tasks {
        static Task<>
        T1(TokenQueue<> &q)
        {
            CheckResult(1, "T3:1");
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static Task<>
        T2(TokenQueue<> &q)
        {
            CheckResult(2, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(7, "T4:2");
            results.push_back("T2:2");
        }

        // High-priority task
        static Task<>
        T3(TaskRef t1, TaskRef t2)
        {
            REQUIRE(results.empty());
            results.push_back("T3:1");
            co_await tasks::WhenAll(t1, t2);
            CheckResult(8, "T2:2");
            results.push_back("T3:2");
        }

        static Task<>
        T4(TokenQueue<> &q1, TokenQueue<> &q2, TokenQueue<> &q5)
        {
            CheckResult(3, "T2:1");
            results.push_back("T4:1");
            q1.Push();
            co_await tasks::Switch();
            CheckResult(6, "T5:1");
            results.push_back("T4:2");
            q2.Push();
            q5.Push();
            co_return;
        }

        static Task<>
        T5(TokenQueue<> &q)
        {
            CheckResult(5, "T1:2");
            results.push_back("T5:1");
            co_await q;
            CheckResult(9, "T3:2");
            results.push_back("T5:2");
        }
    };

    TokenQueue<> q1, q2, q5;

    results.clear();

    auto t1 = tasks::Spawn(Tasks::T1(q1));

    auto t2 = tasks::Spawn(Tasks::T2(q2));

    auto t3 = tasks::Spawn(Tasks::T3(t1, t2), 0);

    auto t4 = tasks::Spawn(Tasks::T4(q1, q2, q5));

    auto t5 = tasks::Spawn(Tasks::T5(q5));

    tasks::RunSome();

    CheckResult(10, "T5:2");
}


TEST_CASE("WhenAny")
{
    struct Tasks {
        static Task<>
        T1(TokenQueue<> &q)
        {
            CheckResult(1, "T3:1");
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static Task<>
        T2(TokenQueue<> &q)
        {
            CheckResult(2, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(7, "T4:2");
            results.push_back("T2:2");
        }

        static Task<>
        T3(TaskRef t1, TaskRef t2)
        {
            REQUIRE(results.empty());
            results.push_back("T3:1");
            // Check awaiter passing.
            co_await tasks::WhenAny(t1, t2);
            CheckResult(5, "T1:2");
            results.push_back("T3:2");
        }

        static Task<>
        T4(TokenQueue<> &q1, TokenQueue<> &q2)
        {
            CheckResult(3, "T2:1");
            results.push_back("T4:1");
            q1.Push();
            co_await tasks::Switch();
            CheckResult(6, "T3:2");
            results.push_back("T4:2");
            q2.Push();
            co_return;
        }
    };

    TokenQueue<> q1, q2;

    results.clear();

    auto t1 = tasks::Spawn(Tasks::T1(q1));

    auto t2 = tasks::Spawn(Tasks::T2(q2));

    auto t3 = tasks::Spawn(Tasks::T3(t1, t2), 0);

    auto t4 = tasks::Spawn(Tasks::T4(q1, q2));

    tasks::RunSome();

    CheckResult(8, "T2:2");
}


TEST_CASE("WhenAny - dynamic")
{
    struct Tasks {
        static Task<>
        T1(TokenQueue<> &q)
        {
            CheckResult(1, "T3:1");
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static Task<>
        T2(TokenQueue<> &q)
        {
            CheckResult(2, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(7, "T4:2");
            results.push_back("T2:2");
        }

        static Task<>
        T3(TaskRef t1, TaskRef t2)
        {
            REQUIRE(results.empty());
            results.push_back("T3:1");
            TaskRef tasks[2] = {t1, t2};
            co_await tasks::WhenAny(etl::span(tasks));
            CheckResult(5, "T1:2");
            results.push_back("T3:2");
        }

        static Task<>
        T4(TokenQueue<> &q1, TokenQueue<> &q2)
        {
            CheckResult(3, "T2:1");
            results.push_back("T4:1");
            q1.Push();
            co_await tasks::Switch();
            CheckResult(6, "T3:2");
            results.push_back("T4:2");
            q2.Push();
            co_return;
        }
    };

    TokenQueue<> q1, q2;

    results.clear();

    auto t1 = tasks::Spawn(Tasks::T1(q1));

    auto t2 = tasks::Spawn(Tasks::T2(q2));

    auto t3 = tasks::Spawn(Tasks::T3(t1, t2), 0);

    auto t4 = tasks::Spawn(Tasks::T4(q1, q2));

    tasks::RunSome();

    CheckResult(8, "T2:2");
}


TEST_CASE("WhenAny - SaveResult")
{
    struct Tasks {
        static Task<int>
        T1(TokenQueue<> &q)
        {
            CheckResult(1, "T3:1");
            results.push_back("T1:1");
            size_t t = co_await q;
            REQUIRE(t == 10);
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
            co_return t;
        }

        static Task<int>
        T2(TokenQueue<> &q)
        {
            CheckResult(2, "T1:1");
            results.push_back("T2:1");
            size_t t = co_await q;
            REQUIRE(t == 20);
            CheckResult(7, "T4:2");
            results.push_back("T2:2");
            co_return t;
        }

        static Task<>
        T3(Task<int> t1, Task<int> t2)
        {
            REQUIRE(results.empty());
            results.push_back("T3:1");
            size_t r1, r2;
            size_t idx = co_await tasks::WhenAny(
                tasks::SaveResult(t1, r1), tasks::SaveResult(t2, r2));
            REQUIRE(idx == 0);
            REQUIRE(r1 == 10);
            CheckResult(5, "T1:2");
            results.push_back("T3:2");
        }

        static Task<>
        T4(TokenQueue<> &q1, TokenQueue<> &q2)
        {
            CheckResult(3, "T2:1");
            results.push_back("T4:1");
            q1.Push();
            co_await tasks::Switch();
            CheckResult(6, "T3:2");
            results.push_back("T4:2");
            q2.Push();
            co_return;
        }
    };

    TokenQueue<> q1(1, 10), q2(1, 20);

    results.clear();

    auto t1 = tasks::Spawn(Tasks::T1(q1));

    auto t2 = tasks::Spawn(Tasks::T2(q2));

    auto t3 = tasks::Spawn(Tasks::T3(t1, t2), 0);

    auto t4 = tasks::Spawn(Tasks::T4(q1, q2));

    tasks::RunSome();

    CheckResult(8, "T2:2");
}


TEST_CASE("WhenAny - mixed task/awaiters")
{
    struct Tasks {
        static Task<>
        T1(TokenQueue<> &q)
        {
            CheckResult(2, "T2:1");
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static Task<>
        T2(TokenQueue<> &q)
        {
            CheckResult(1, "T3:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(7, "T4:2");
            results.push_back("T2:2");
        }

        // High-priority task
        static Task<>
        T3(TaskRef t1, TaskRef t2)
        {
            REQUIRE(results.empty());
            results.push_back("T3:1");
            // Check awaiter passing. Calling `Wait()` directly propagates this task priority.
            // Passing task to WhenAny just adds it to the waiting list, priority is not propagated
            // in WhenAny.
            co_await tasks::WhenAny(t1, t2.Wait());
            CheckResult(5, "T1:2");
            results.push_back("T3:2");
        }

        static Task<>
        T4(TokenQueue<> &q1, TokenQueue<> &q2)
        {
            CheckResult(3, "T1:1");
            results.push_back("T4:1");
            q1.Push();
            co_await tasks::Switch();
            CheckResult(6, "T3:2");
            results.push_back("T4:2");
            q2.Push();
            co_return;
        }
    };

    TokenQueue<> q1, q2;

    results.clear();

    auto t1 = tasks::Spawn(Tasks::T1(q1));

    auto t2 = tasks::Spawn(Tasks::T2(q2));

    auto t3 = tasks::Spawn(Tasks::T3(t1, t2), 0);

    auto t4 = tasks::Spawn(Tasks::T4(q1, q2));

    tasks::RunSome();

    CheckResult(8, "T2:2");
}


TEST_CASE("WhenAny - awaiters")
{
    struct Tasks {
        static Task<>
        T1(TokenQueue<> &q)
        {
            CheckResult(1, "T3:1");
            results.push_back("T1:1");
            co_await q;
            CheckResult(4, "T4:1");
            results.push_back("T1:2");
        }

        static Task<>
        T2(TokenQueue<> &q)
        {
            CheckResult(2, "T1:1");
            results.push_back("T2:1");
            co_await q;
            // should not be reached
            FAIL();
        }

        static Task<>
        T3(TaskRef t1, TaskRef t2)
        {
            REQUIRE(results.empty());
            results.push_back("T3:1");
            // Check awaiter passing.
            co_await tasks::WhenAny(t1.Wait(), t2.Wait());
            CheckResult(5, "T1:2");
            results.push_back("T3:2");
        }

        static Task<>
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

    auto t1 = tasks::Spawn(Tasks::T1(q1));

    auto t2 = tasks::Spawn(Tasks::T2(q2));

    auto t3 = tasks::Spawn(Tasks::T3(t1, t2), 0);

    auto t4 = tasks::Spawn(Tasks::T4(q1));

    tasks::RunSome();

    CheckResult(6, "T3:2");
}


TEST_CASE("WhenAny - completed")
{
    struct Tasks {
        static Task<>
        T1()
        {
            REQUIRE(results.empty());
            results.push_back("T1:1");
            co_return;
        }

        static Task<>
        T2(TokenQueue<> &q)
        {
            CheckResult(1, "T1:1");
            results.push_back("T2:1");
            co_await q;
            CheckResult(4, "T3:2");
            results.push_back("T2:2");
        }

        static Task<>
        T3(TaskRef t1, TaskRef t2, TokenQueue<> &q)
        {
            CheckResult(2, "T2:1");
            results.push_back("T3:1");
            co_await tasks::WhenAny(t1, t2.Wait());
            CheckResult(3, "T3:1");
            results.push_back("T3:2");
            q.Push();
        }
    };

    TokenQueue<> q;

    results.clear();

    auto t1 = tasks::Spawn(Tasks::T1());

    auto t2 = tasks::Spawn(Tasks::T2(q));

    auto t3 = tasks::Spawn(Tasks::T3(t1, t2, q));

    tasks::RunSome();

    CheckResult(5, "T2:2");
}


TEST_CASE("WhenAny - stress")
{
    struct Tasks {
        static Task<>
        T1(TokenQueue<> &q)
        {
            Timer timer;

            while (true) {
                timer.ExpiresAfter(10);
                size_t idx = co_await tasks::WhenAny(q, timer);
                REQUIRE(idx == 0);
                if (q.Peek() >= 1000) {
                    break;
                }
            }

            timer.Cancel();
        }

        static Task<>
        T2(TokenQueue<> &q)
        {
            for (int i = 0; i < 1010; i++) {
                q.Push();
                co_await tasks::Switch();
            }
        }
    };

    TokenQueue<> q;

    auto t1 = tasks::Spawn(Tasks::T1(q));

    auto t2 = tasks::Spawn(Tasks::T2(q));

    tasks::RunSome();

    REQUIRE(t1.IsFinished());
    REQUIRE(t2.IsFinished());
}


TEST_CASE("TaskWeakPtr")
{
    TaskWeakRef p;
    {
        Task t = ([]() -> Task<int> {
            co_return 42;
        })();
        p = t.GetWeakPtr();
        REQUIRE(p);
        REQUIRE(p.Lock() == t);

        TaskWeakRef p2 = etl::move(p);
        REQUIRE(!p);
        REQUIRE(!p.Lock());
        REQUIRE(p2);
        REQUIRE(p2.Lock() == t);

        p = p2;
        REQUIRE(p);
        REQUIRE(p.Lock() == t);
        REQUIRE(p2);
        REQUIRE(p2.Lock() == t);
    }
    REQUIRE(!p.Lock());
}


TEST_CASE("Task moveable return type")
{
    auto t1 = tasks::Spawn([]() -> Task<std::unique_ptr<int>> {
        co_return std::make_unique<int>(42);
    });

    auto t2 = tasks::Spawn([](auto &t1) -> Task<std::unique_ptr<int>> {
        co_return co_await t1;
    }, t1);

    tasks::RunSome();

    REQUIRE(t1.IsFinished());
    // Has been moved out.
    REQUIRE(!t1.GetResult());
    REQUIRE(*t2.GetResult() == 42);
}
