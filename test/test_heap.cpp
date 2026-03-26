#include <catch2/catch_test_macros.hpp>
#include <pulse/heap.h>
#include <set>


using namespace pulse;


static constexpr auto MaxHeapCmp = [](const int& a, const int& b) {
    return a > b;
};

static constexpr auto MinHeapCmp = [](const int& a, const int& b) {
    return a < b;
};


TEST_CASE("Insert top max-heap")
{
    Heap<int, MaxHeapCmp, 16> h;

    assert(h.Insert(1));
    assert(h.Top() == 1);

    assert(h.Insert(5));
    assert(h.Top() == 5);

    assert(h.Insert(3));
    assert(h.Top() == 5);
}


TEST_CASE("PopOrder_MaxHeap")
{
    Heap<int, MaxHeapCmp, 16> h;

    int values[] = {3, 1, 6, 5, 2, 4};
    for (int v : values)
        h.Insert(v);

    int expected[] = {6, 5, 4, 3, 2, 1};

    for (int e : expected) {
        assert(h.Top() == e);
        h.PopTop();
    }

    assert(h.Size() == 0);
}


TEST_CASE("PopOrder_MinHeap")
{
    Heap<int, MinHeapCmp, 16> h;

    int values[] = {3, 1, 6, 5, 2, 4};
    for (int v : values)
        h.Insert(v);

    int expected[] = {1, 2, 3, 4, 5, 6};

    for (int e : expected) {
        assert(h.Top() == e);
        h.PopTop();
    }
}


TEST_CASE("Capacity")
{
    Heap<int, MaxHeapCmp, 3> h;

    assert(h.Insert(1));
    assert(h.Insert(2));
    assert(h.Insert(3));
    assert(!h.Insert(4)); // must fail

    assert(h.Size() == 3);
}


TEST_CASE("SingleElement")
{
    Heap<int, MaxHeapCmp, 4> h;

    h.Insert(42);
    assert(h.Top() == 42);

    h.PopTop();
    assert(h.Size() == 0);
}


TEST_CASE("Duplicates")
{
    Heap<int, MaxHeapCmp, 10> h;

    h.Insert(5);
    h.Insert(5);
    h.Insert(5);

    assert(h.Top() == 5);

    h.PopTop();
    assert(h.Top() == 5);

    h.PopTop();
    assert(h.Top() == 5);

    h.PopTop();
    assert(h.Size() == 0);
}


TEST_CASE("Interleaved")
{
    Heap<int, MaxHeapCmp, 10> h;

    h.Insert(3);
    h.Insert(10);
    assert(h.Top() == 10);

    h.PopTop();
    assert(h.Top() == 3);

    h.Insert(8);
    assert(h.Top() == 8);

    h.Insert(15);
    assert(h.Top() == 15);
}


namespace {

struct Tracker {
    static int constructed,
               destroyed,
               optionalDestroyed;

    std::optional<int> value;
    size_t index = 42;

    Tracker(int v) : value(v) { constructed++; }
    Tracker(const Tracker& o) : value(o.value) { constructed++; }

    Tracker(Tracker&& o) noexcept:
        value(o.value)
    {
        o.value = std::nullopt;
        constructed++;
    }

    Tracker &
    operator =(const Tracker &other)
    {
        if (value) {
            optionalDestroyed++;
        }
        value = other.value;
        return *this;
    }

    Tracker &
    operator =(Tracker &&other) noexcept
    {
        if (value) {
            optionalDestroyed++;
        }
        value = other.value;
        other.value = std::nullopt;
        return *this;
    }

    ~Tracker()
    {
        destroyed++;
        if (value) {
            optionalDestroyed++;
        }
    }

    bool operator >(const Tracker& other) const
    {
        return *value > *other.value;
    }
};

int Tracker::constructed = 0;
int Tracker::destroyed = 0;
int Tracker::optionalDestroyed = 0;

static bool
TrackerCmp(const Tracker& a, const Tracker& b)
{
    return *a.value > *b.value;
};

static void
TrackerSetIndex(Tracker &t, size_t index)
{
    t.index = index;
}

} // anonymous namespace

TEST_CASE("Lifetime")
{
    Tracker::constructed = 0;
    Tracker::destroyed = 0;
    Tracker::optionalDestroyed = 0;

    constexpr size_t N = 10;

    {
        Heap<Tracker, TrackerCmp, N, TrackerSetIndex> h;

        for (int i = 0; i < N; i++) {
            REQUIRE(h.Insert(i));
        }

        for (int i = N - 1; i >= 0; --i) {
            REQUIRE(*h.Top().value == i);
            REQUIRE(h.Top().index == 0);
            h.PopTop();
        }
    }

    REQUIRE(Tracker::constructed == Tracker::destroyed);
    REQUIRE(Tracker::optionalDestroyed == N);
}

TEST_CASE("Remove")
{
    Tracker::constructed = 0;
    Tracker::destroyed = 0;
    Tracker::optionalDestroyed = 0;

    constexpr size_t N = 20;

    {
        Heap<Tracker, TrackerCmp, N, TrackerSetIndex> h;

        for (int i = 0; i < N; i++) {
            REQUIRE(h.Insert(i));
        }

        std::vector<int> removedIdx{3, 17, 5, 15, 10};
        std::set<int> removed{};
        for (int idx: removedIdx) {
            Tracker &t = h.Item(idx);
            REQUIRE(idx == t.index);
            removed.emplace(*t.value);
            h.Remove(idx);
        }
        REQUIRE(h.Size() == N - removed.size());

        for (int idx = 0; idx < h.Size(); idx++) {
            REQUIRE(idx == h.Item(idx).index);
        }

        for (int i = N - 1; i >= 0; --i) {
            if (removed.contains(i)) {
                continue;
            }
            REQUIRE(*h.Top().value == i);
            REQUIRE(h.Top().index == 0);
            h.PopTop();
        }
    }

    REQUIRE(Tracker::constructed == Tracker::destroyed);
    REQUIRE(Tracker::optionalDestroyed == N);
}

TEST_CASE("Stress")
{
    Heap<int, MaxHeapCmp, 128> h;

    for (int i = 0; i < 128; ++i) {
        REQUIRE(h.Insert(i));
    }

    for (int i = 127; i >= 0; --i) {
        REQUIRE(h.Top() == i);
        h.PopTop();
    }
}
