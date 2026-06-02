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

    auto t1 = tasks::Spawn([](Semaphore<> &sem) -> Task<> {
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

    auto t2 = tasks::Spawn([](Semaphore<> &sem) -> Task<> {
        CheckResult(3, "T1:3");
        results.push_back("T2:1");
        sem.Release();
        while (co_await tasks::Switch());
        CheckResult(5, "T1:4");
        results.push_back("T2:2");
    }, sem);

    tasks::RunSome();

    CheckResult(6, "T2:2");
}

TEST_CASE("Semaphore release wakes only one waiter per token")
{
    results.clear();

    Semaphore<> sem(1);

    // Holder grabs the only token, lets the two waiters queue up, then releases
    // a single token.
    auto holder = tasks::Spawn([](Semaphore<> &sem) -> Task<> {
        bool b = co_await sem.Acquire();
        REQUIRE(b);
        // Yield so both waiters get a chance to suspend on Acquire().
        co_await tasks::Switch();
        // Free exactly one token. Only one waiter must be granted it.
        sem.Release();
    }, sem);

    auto waiter1 = tasks::Spawn([](Semaphore<> &sem) -> Task<> {
        bool b = co_await sem.Acquire();
        REQUIRE(b);
        results.push_back("W1");
    }, sem);

    auto waiter2 = tasks::Spawn([](Semaphore<> &sem) -> Task<> {
        bool b = co_await sem.Acquire();
        REQUIRE(b);
        results.push_back("W2");
    }, sem);

    tasks::RunSome();

    // A single Release() frees one token, so exactly one waiter may proceed; the
    // other must stay blocked. With the missing numAcquired++ in Release(), both
    // waiters wake up and the semaphore is oversubscribed.
    REQUIRE(results.size() == 1);
}

TEST_CASE("Semaphore awaiter destruction")
{
    Semaphore<> sem(2);

    auto t1 = tasks::Spawn([](Semaphore<> &sem) -> Task<> {
        sem.Acquire();
        sem.Acquire();
        sem.Acquire();
        co_return;
    }, sem);

    tasks::RunSome();

    REQUIRE(t1.IsFinished());
}

TEST_CASE("Semaphore destruction")
{
    etl::optional<Semaphore<>> sem;
    sem.emplace(2, 1);

    auto t1 = tasks::Spawn([](etl::optional<Semaphore<>> &sem) -> Task<> {
        {
            auto g = co_await sem->AcquireGuard();
            REQUIRE(g);
        }
        REQUIRE(sem->TryAcquire());
        REQUIRE(!sem->TryAcquire());
        bool b = co_await sem->Acquire();
        REQUIRE(!b);
    }, sem);

    auto t2 = tasks::Spawn([](etl::optional<Semaphore<>> &sem) -> Task<> {
        sem.reset();
        co_return;
    }, sem);

    tasks::RunSome();

    REQUIRE(t1.IsFinished());
    REQUIRE(t2.IsFinished());
}
