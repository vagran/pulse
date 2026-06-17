#include <catch2/catch_test_macros.hpp>
#include <pulse/pool.h>

#include <set>


using namespace pulse;


namespace {

/// Counts construction/destruction so we can verify the pool drives object lifetime correctly.
struct Tracker {
    static int constructed,
               destroyed;

    int value;

    Tracker(int v = 0) : value(v) { constructed++; }
    Tracker(const Tracker &other) : value(other.value) { constructed++; }

    ~Tracker() { destroyed++; }

    static void
    Reset()
    {
        constructed = 0;
        destroyed = 0;
    }

    static int
    Alive()
    {
        return constructed - destroyed;
    }
};

int Tracker::constructed = 0;
int Tracker::destroyed = 0;


/// Trait that counts each hook invocation.
struct CountingTrait {
    static int allocated,
               poolAllocated,
               dynamicallyAllocated,
               freed;

    static void OnAllocated() { allocated++; }
    static void OnPoolAllocated() { poolAllocated++; }
    static void OnDynamicallyAllocated() { dynamicallyAllocated++; }
    static void OnFreed() { freed++; }

    static void
    Reset()
    {
        allocated = 0;
        poolAllocated = 0;
        dynamicallyAllocated = 0;
        freed = 0;
    }
};

int CountingTrait::allocated = 0;
int CountingTrait::poolAllocated = 0;
int CountingTrait::dynamicallyAllocated = 0;
int CountingTrait::freed = 0;


/// Guard that counts how many times it is constructed/destroyed (i.e. critical sections entered).
struct CountingGuard {
    static int entered,
               left;

    CountingGuard() { entered++; }
    ~CountingGuard() { left++; }

    static void
    Reset()
    {
        entered = 0;
        left = 0;
    }
};

int CountingGuard::entered = 0;
int CountingGuard::left = 0;

} // anonymous namespace


TEST_CASE("Allocate and free single item")
{
    Pool<int, 4> pool;

    int *p = pool.Allocate(42);
    REQUIRE(p != nullptr);
    REQUIRE(*p == 42);

    pool.Free(p);
}


TEST_CASE("Allocate forwards constructor arguments")
{
    struct Pair {
        int a, b;
        Pair(int a, int b) : a(a), b(b) {}
    };

    Pool<Pair, 4> pool;

    Pair *p = pool.Allocate(3, 7);
    REQUIRE(p != nullptr);
    REQUIRE(p->a == 3);
    REQUIRE(p->b == 7);

    pool.Free(p);
}


TEST_CASE("Exhaustion returns nullptr without dynamic allocation")
{
    constexpr size_t N = 3;
    Pool<int, N> pool;

    int *items[N];
    for (size_t i = 0; i < N; i++) {
        items[i] = pool.Allocate(static_cast<int>(i));
        REQUIRE(items[i] != nullptr);
    }

    // Pool is exhausted, dynamic allocation disabled.
    REQUIRE(pool.Allocate(123) == nullptr);

    for (size_t i = 0; i < N; i++) {
        pool.Free(items[i]);
    }
}


TEST_CASE("Freed items are reused")
{
    Pool<int, 2> pool;

    int *a = pool.Allocate(1);
    int *b = pool.Allocate(2);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(pool.Allocate(3) == nullptr);

    pool.Free(a);

    // The slot just freed must become available again.
    int *c = pool.Allocate(3);
    REQUIRE(c != nullptr);
    REQUIRE(c == a);
    REQUIRE(*c == 3);

    pool.Free(b);
    pool.Free(c);
}


TEST_CASE("All allocations come from pool storage")
{
    constexpr size_t N = 8;
    Pool<int, N> pool;

    int *items[N];
    for (size_t i = 0; i < N; i++) {
        items[i] = pool.Allocate(static_cast<int>(i));
        REQUIRE(items[i] != nullptr);
    }

    // All returned pointers must be distinct.
    std::set<int *> unique(items, items + N);
    REQUIRE(unique.size() == N);

    for (size_t i = 0; i < N; i++) {
        pool.Free(items[i]);
    }
}


TEST_CASE("Allocate constructs, Free destroys")
{
    Tracker::Reset();

    {
        Pool<Tracker, 4> pool;

        Tracker *t = pool.Allocate(5);
        REQUIRE(t->value == 5);
        REQUIRE(Tracker::constructed == 1);
        REQUIRE(Tracker::destroyed == 0);

        pool.Free(t);
        REQUIRE(Tracker::destroyed == 1);
    }

    REQUIRE(Tracker::Alive() == 0);
}


TEST_CASE("New does not construct, Delete does not destroy")
{
    Tracker::Reset();

    Pool<Tracker, 4> pool;

    void *mem = pool.New(sizeof(Tracker));
    REQUIRE(mem != nullptr);
    REQUIRE(Tracker::constructed == 0);

    pool.Delete(mem, sizeof(Tracker));
    REQUIRE(Tracker::destroyed == 0);
}


TEST_CASE("New returns nullptr on exhaustion without dynamic allocation")
{
    Pool<int, 1> pool;

    void *a = pool.New(sizeof(int));
    REQUIRE(a != nullptr);
    REQUIRE(pool.New(sizeof(int)) == nullptr);

    pool.Delete(a, sizeof(int));
}


TEST_CASE("Dynamic allocation extends the pool")
{
    constexpr size_t N = 2;
    Pool<int, N, /* allowDynamicAlloc */ true> pool;

    std::vector<int *> items;
    for (int i = 0; i < 6; i++) {
        int *p = pool.Allocate(i);
        REQUIRE(p != nullptr);
        REQUIRE(*p == i);
        items.push_back(p);
    }

    std::set<int *> unique(items.begin(), items.end());
    REQUIRE(unique.size() == items.size());

    for (int *p : items) {
        pool.Free(p);
    }
}


TEST_CASE("Dynamically allocated items refill the free list")
{
    constexpr size_t N = 1;
    Pool<int, N, true> pool;

    int *a = pool.Allocate(1); // from pool
    int *b = pool.Allocate(2); // dynamic
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    // Freeing the dynamic item puts it on the free list, so the next allocation reuses it
    // instead of allocating again.
    pool.Free(b);
    int *c = pool.Allocate(3);
    REQUIRE(c == b);
    REQUIRE(*c == 3);

    pool.Free(a);
    pool.Free(c);
}


TEST_CASE("Trait hooks fire for pool allocations")
{
    CountingTrait::Reset();

    {
        Pool<int, 2, false, details::PoolNopGuard, CountingTrait> pool;

        int *a = pool.Allocate(1);
        int *b = pool.Allocate(2);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);

        REQUIRE(CountingTrait::allocated == 2);
        REQUIRE(CountingTrait::poolAllocated == 2);
        REQUIRE(CountingTrait::dynamicallyAllocated == 0);
        REQUIRE(CountingTrait::freed == 0);

        // Exhausted, no dynamic allocation: no hooks fire on failure.
        REQUIRE(pool.Allocate(3) == nullptr);
        REQUIRE(CountingTrait::allocated == 2);

        pool.Free(a);
        pool.Free(b);
        REQUIRE(CountingTrait::freed == 2);
    }
}


TEST_CASE("Trait distinguishes dynamic from pool allocations")
{
    CountingTrait::Reset();

    Pool<int, 1, true, details::PoolNopGuard, CountingTrait> pool;

    int *a = pool.Allocate(1); // pool
    int *b = pool.Allocate(2); // dynamic
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    REQUIRE(CountingTrait::allocated == 2);
    REQUIRE(CountingTrait::poolAllocated == 1);
    REQUIRE(CountingTrait::dynamicallyAllocated == 1);

    pool.Free(a);
    pool.Free(b);
    REQUIRE(CountingTrait::freed == 2);
}


TEST_CASE("Guard is entered on every allocation and free")
{
    CountingGuard::Reset();

    {
        Pool<int, 4, false, CountingGuard> pool;

        int *a = pool.Allocate(1);
        int *b = pool.Allocate(2);
        pool.Free(a);
        pool.Free(b);

        // One guarded critical section per New and per Delete.
        REQUIRE(CountingGuard::entered == 4);
        REQUIRE(CountingGuard::left == 4);
    }
}


TEST_CASE("Zero-sized pool always fails without dynamic allocation")
{
    Pool<int, 0> pool;
    REQUIRE(pool.Allocate(1) == nullptr);
    REQUIRE(pool.New(sizeof(int)) == nullptr);
}


TEST_CASE("Zero-sized pool works with dynamic allocation")
{
    Pool<int, 0, true> pool;

    int *a = pool.Allocate(7);
    REQUIRE(a != nullptr);
    REQUIRE(*a == 7);

    pool.Free(a);
}


TEST_CASE("Repeated allocate/free cycles")
{
    Tracker::Reset();

    {
        constexpr size_t N = 4;
        Pool<Tracker, N> pool;

        for (int cycle = 0; cycle < 100; cycle++) {
            Tracker *items[N];
            for (size_t i = 0; i < N; i++) {
                items[i] = pool.Allocate(static_cast<int>(cycle + i));
                REQUIRE(items[i] != nullptr);
            }
            REQUIRE(pool.Allocate(0) == nullptr);
            for (size_t i = 0; i < N; i++) {
                REQUIRE(items[i]->value == static_cast<int>(cycle + i));
                pool.Free(items[i]);
            }
        }
    }

    REQUIRE(Tracker::Alive() == 0);
}
