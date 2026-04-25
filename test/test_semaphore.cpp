#include <catch2/catch_test_macros.hpp>
#include <pulse/semaphore.h>
#include <pulse/token_queue.h>


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

} // anonymous namespace


TEST_CASE("Semaphore")
{
    results.clear();

    Semaphore<> sem(2);

    auto t1 = Task::Spawn([](Semaphore<> &sem) -> TaskV {
        REQUIRE(results.empty());
        results.push_back("T1:1");
        bool b = co_await sem.Acquire();
        REQUIRE(b);
        CheckResult(1, "T1:1");
        results.push_back("T1:2");
        b = co_await sem.Acquire();
        REQUIRE(b);
        CheckResult(2, "T1:2");
        results.push_back("T1:3");
        b = co_await sem.Acquire();
        REQUIRE(b);
        CheckResult(4, "T2:1");
        results.push_back("T1:4");
    }, sem);

    auto t2 = Task::Spawn([](Semaphore<> &sem) -> TaskV {
        CheckResult(3, "T1:3");
        results.push_back("T2:1");
        sem.Release();
        while (co_await Task::Switch());
        CheckResult(5, "T1:4");
        results.push_back("T2:2");
    }, sem);

    Task::RunSome();

    CheckResult(6, "T2:2");
}

TEST_CASE("Semaphore awaiter destruction")
{
    Semaphore<> sem(2);

    auto t1 = Task::Spawn([](Semaphore<> &sem) -> TaskV {
        sem.Acquire();
        sem.Acquire();
        sem.Acquire();
        co_return;
    }, sem);

    Task::RunSome();

    REQUIRE(t1.IsFinished());
}

TEST_CASE("Semaphore destruction")
{
    etl::optional<Semaphore<>> sem;
    sem.emplace(2, 1);

    auto t1 = Task::Spawn([](etl::optional<Semaphore<>> &sem) -> TaskV {
        {
            auto g = co_await sem->AcquireGuard();
            REQUIRE(g);
        }
        REQUIRE(sem->TryAcquire());
        REQUIRE(!sem->TryAcquire());
        bool b = co_await sem->Acquire();
        REQUIRE(!b);
    }, sem);

    auto t2 = Task::Spawn([](etl::optional<Semaphore<>> &sem) -> TaskV {
        sem.reset();
        co_return;
    }, sem);

    Task::RunSome();

    REQUIRE(t1.IsFinished());
    REQUIRE(t2.IsFinished());
}
