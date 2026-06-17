#include <catch2/catch_test_macros.hpp>
#include <pulse/unique_ptr.h>

#include <etl/utility.h>


using namespace pulse;


namespace {

/// Counts construction/destruction so we can verify the pointer drives object lifetime correctly.
struct Tracker {
    static int constructed,
               destroyed;

    int value;

    Tracker(int v = 0) : value(v) { constructed++; }

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


/// Trait that counts how many times an object was deleted through it.
struct CountingTrait {
    static int deleted;

    static void
    Delete(Tracker *obj)
    {
        deleted++;
        delete obj;
    }

    static void
    Reset()
    {
        deleted = 0;
    }
};

int CountingTrait::deleted = 0;

} // anonymous namespace


TEST_CASE("Default constructed pointer is empty")
{
    UniquePtr<Tracker> p;
    REQUIRE(p.Get() == nullptr);
    REQUIRE(!p);
    REQUIRE(p == nullptr);
    REQUIRE_FALSE(p != nullptr);
}


TEST_CASE("Construction from nullptr is empty")
{
    UniquePtr<Tracker> p(nullptr);
    REQUIRE(p.Get() == nullptr);
    REQUIRE(!p);
}


TEST_CASE("Owns object and destroys it on scope exit")
{
    Tracker::Reset();

    {
        UniquePtr<Tracker> p(new Tracker(42));
        REQUIRE(p);
        REQUIRE(p != nullptr);
        REQUIRE(p.Get() != nullptr);
        REQUIRE(p->value == 42);
        REQUIRE((*p).value == 42);
        REQUIRE(Tracker::Alive() == 1);
    }

    REQUIRE(Tracker::Alive() == 0);
}


TEST_CASE("Move construction transfers ownership")
{
    Tracker::Reset();

    {
        UniquePtr<Tracker> a(new Tracker(7));
        Tracker *raw = a.Get();

        UniquePtr<Tracker> b(etl::move(a));
        REQUIRE(a.Get() == nullptr);
        REQUIRE(b.Get() == raw);
        REQUIRE(b->value == 7);
        REQUIRE(Tracker::Alive() == 1);
    }

    REQUIRE(Tracker::Alive() == 0);
}


TEST_CASE("Move assignment transfers ownership and frees the old object")
{
    Tracker::Reset();

    {
        UniquePtr<Tracker> a(new Tracker(1));
        UniquePtr<Tracker> b(new Tracker(2));
        Tracker *raw = b.Get();
        REQUIRE(Tracker::Alive() == 2);

        a = etl::move(b);

        // The object previously held by `a` must be destroyed.
        REQUIRE(Tracker::Alive() == 1);
        REQUIRE(a.Get() == raw);
        REQUIRE(a->value == 2);
        REQUIRE(b.Get() == nullptr);
    }

    REQUIRE(Tracker::Alive() == 0);
}


TEST_CASE("Self move assignment keeps the object")
{
    Tracker::Reset();

    {
        UniquePtr<Tracker> a(new Tracker(5));
        Tracker *raw = a.Get();

        UniquePtr<Tracker> &ref = a;
        a = etl::move(ref);

        REQUIRE(a.Get() == raw);
        REQUIRE(a->value == 5);
        REQUIRE(Tracker::Alive() == 1);
    }

    REQUIRE(Tracker::Alive() == 0);
}


TEST_CASE("Assigning nullptr frees the object")
{
    Tracker::Reset();

    UniquePtr<Tracker> p(new Tracker(9));
    REQUIRE(Tracker::Alive() == 1);

    p = nullptr;
    REQUIRE(Tracker::Alive() == 0);
    REQUIRE(p.Get() == nullptr);
}


TEST_CASE("Reset frees the owned object")
{
    Tracker::Reset();

    UniquePtr<Tracker> p(new Tracker(3));
    REQUIRE(Tracker::Alive() == 1);

    p.Reset();
    REQUIRE(Tracker::Alive() == 0);
    REQUIRE(p.Get() == nullptr);

    // Resetting an empty pointer is a no-op.
    p.Reset();
    REQUIRE(Tracker::Alive() == 0);
}


TEST_CASE("Reset takes ownership of a new object")
{
    Tracker::Reset();

    UniquePtr<Tracker> p(new Tracker(1));
    Tracker *next = new Tracker(2);

    p.Reset(next);

    // The first object is freed, the new one is owned.
    REQUIRE(Tracker::Alive() == 1);
    REQUIRE(p.Get() == next);
    REQUIRE(p->value == 2);
}


TEST_CASE("Release relinquishes ownership without freeing")
{
    Tracker::Reset();

    Tracker *raw;
    {
        UniquePtr<Tracker> p(new Tracker(4));
        raw = p.Release();
        REQUIRE(p.Get() == nullptr);
        REQUIRE(!p);
    }

    // Pointer went out of scope but did not delete the released object.
    REQUIRE(Tracker::Alive() == 1);
    REQUIRE(raw->value == 4);

    delete raw;
    REQUIRE(Tracker::Alive() == 0);
}


TEST_CASE("Swap exchanges owned objects")
{
    Tracker::Reset();

    {
        UniquePtr<Tracker> a(new Tracker(1));
        UniquePtr<Tracker> b(new Tracker(2));
        Tracker *ra = a.Get();
        Tracker *rb = b.Get();

        a.Swap(b);

        REQUIRE(a.Get() == rb);
        REQUIRE(b.Get() == ra);
        REQUIRE(a->value == 2);
        REQUIRE(b->value == 1);
        REQUIRE(Tracker::Alive() == 2);
    }

    REQUIRE(Tracker::Alive() == 0);
}


TEST_CASE("Equality compares the owned pointers")
{
    UniquePtr<Tracker> a(new Tracker(1));
    UniquePtr<Tracker> empty;

    REQUIRE(a == a);
    REQUIRE(a != empty);
    REQUIRE(empty == empty);
}


TEST_CASE("Custom trait is used for deletion")
{
    Tracker::Reset();
    CountingTrait::Reset();

    {
        UniquePtr<Tracker, CountingTrait> p(new Tracker(1));
        REQUIRE(CountingTrait::deleted == 0);
    }

    REQUIRE(CountingTrait::deleted == 1);
    REQUIRE(Tracker::Alive() == 0);
}
