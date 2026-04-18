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

    auto t1 = Task::Spawn([&]() -> TaskV {
        REQUIRE(results.empty());
        results.push_back("T1:1");
        REQUIRE(co_await sem.Acquire());
        CheckResult(1, "T1:1");
        results.push_back("T1:2");
        REQUIRE(co_await sem.Acquire());
        CheckResult(2, "T1:2");
        results.push_back("T1:3");
        REQUIRE(co_await sem.Acquire());
        CheckResult(4, "T2:1");
        results.push_back("T1:4");
    });

    auto t2 = Task::Spawn([&]() -> TaskV {
        CheckResult(3, "T1:3");
        results.push_back("T2:1");
        sem.Release();
        while (co_await Task::Switch());
        CheckResult(5, "T1:4");
        results.push_back("T2:2");
    });

    Task::RunSome();

    CheckResult(6, "T2:2");
}

TEST_CASE("Semaphore awaiter destruction")
{
    Semaphore<> sem(2);

    auto t1 = Task::Spawn([&]() -> TaskV {
        sem.Acquire();
        sem.Acquire();
        sem.Acquire();
        co_return;
    });

    Task::RunSome();

    REQUIRE(t1.IsFinished());
}

TEST_CASE("Semaphore destruction")
{
    etl::optional<Semaphore<>> sem;
    sem.emplace(2, 1);

    auto t1 = Task::Spawn([&]() -> TaskV {
        //XXX implement moveable task return types
        // {
        //     auto g = co_await sem->AcquireGuard();
        //     REQUIRE(g);
        // }
        REQUIRE(sem->TryAcquire());
        REQUIRE(!sem->TryAcquire());
        REQUIRE(!co_await sem->Acquire());
    });

    auto t2 = Task::Spawn([&]() -> TaskV {
        sem.reset();
        co_return;
    });

    Task::RunSome();

    REQUIRE(t1.IsFinished());
    REQUIRE(t2.IsFinished());
}
