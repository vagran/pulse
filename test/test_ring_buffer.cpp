#include <catch2/catch_test_macros.hpp>
#include <pulse/ring_buffer.h>

#include <memory>

using namespace pulse;


namespace {

/// Counts construction/destruction so we can verify the buffer drives object lifetime correctly.
struct Tracker {
    static int constructed,
               destroyed;

    int value;

    Tracker(int v = 0) : value(v) { constructed++; }
    Tracker(const Tracker &other) : value(other.value) { constructed++; }
    Tracker(Tracker &&other) : value(other.value) { constructed++; }

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

} // anonymous namespace


TEST_CASE("Empty buffer state")
{
    InlineRingBuffer<int, 4> rb;

    REQUIRE(rb.capacity == 4);
    REQUIRE(rb.GetSize() == 0);
    REQUIRE(rb.GetAvailableCapacity() == 4);
    REQUIRE(rb.IsEmpty());
    REQUIRE_FALSE(rb.IsFull());
}


TEST_CASE("Push and pop preserve FIFO order")
{
    InlineRingBuffer<int, 4> rb;

    REQUIRE(rb.Push(1) != nullptr);
    REQUIRE(rb.Push(2) != nullptr);
    REQUIRE(rb.Push(3) != nullptr);

    REQUIRE(rb.GetSize() == 3);
    REQUIRE(rb.GetAvailableCapacity() == 1);

    REQUIRE(rb.Pop() == 1);
    REQUIRE(rb.Pop() == 2);
    REQUIRE(rb.Pop() == 3);
    REQUIRE(rb.IsEmpty());
}


TEST_CASE("Push forwards constructor arguments")
{
    struct Pair {
        int a, b;
        Pair(int a, int b) : a(a), b(b) {}
    };

    InlineRingBuffer<Pair, 4> rb;

    Pair *p = rb.Push(3, 7);
    REQUIRE(p != nullptr);
    REQUIRE(p->a == 3);
    REQUIRE(p->b == 7);
}


TEST_CASE("Push on full buffer returns nullptr")
{
    InlineRingBuffer<int, 2> rb;

    REQUIRE(rb.Push(1) != nullptr);
    REQUIRE(rb.Push(2) != nullptr);
    REQUIRE(rb.IsFull());
    REQUIRE(rb.Push(3) == nullptr);

    // No element was clobbered or lost.
    REQUIRE(rb.GetSize() == 2);
    REQUIRE(rb.Pop() == 1);
    REQUIRE(rb.Pop() == 2);
}


TEST_CASE("Peek returns the front element without removing it")
{
    InlineRingBuffer<int, 4> rb;
    rb.Push(10);
    rb.Push(20);

    REQUIRE(rb.Peek() == 10);
    REQUIRE(rb.GetSize() == 2);

    const auto &crb = rb;
    REQUIRE(crb.Peek() == 10);

    rb.RemoveFirst();
    REQUIRE(rb.Peek() == 20);
    REQUIRE(rb.GetSize() == 1);
}


TEST_CASE("Read/write indices wrap around")
{
    InlineRingBuffer<int, 4> rb;

    // Fill, drain partway, then refill so the write position wraps past the end.
    for (int i = 0; i < 4; i++) {
        REQUIRE(rb.Push(i) != nullptr);
    }
    REQUIRE(rb.Pop() == 0);
    REQUIRE(rb.Pop() == 1);

    REQUIRE(rb.Push(4) != nullptr);
    REQUIRE(rb.Push(5) != nullptr);
    REQUIRE(rb.IsFull());

    REQUIRE(rb.Pop() == 2);
    REQUIRE(rb.Pop() == 3);
    REQUIRE(rb.Pop() == 4);
    REQUIRE(rb.Pop() == 5);
    REQUIRE(rb.IsEmpty());
}


TEST_CASE("Pop destroys the popped element exactly once")
{
    Tracker::Reset();
    {
        InlineRingBuffer<Tracker, 4> rb;
        rb.Push(1);
        rb.Push(2);
        REQUIRE(Tracker::Alive() == 2);

        Tracker t = rb.Pop();
        REQUIRE(t.value == 1);
        // One element left in the buffer, one held in `t`.
        REQUIRE(Tracker::Alive() == 2);
    }
    // Buffer drained on destruction, `t` destroyed at end of scope.
    REQUIRE(Tracker::Alive() == 0);
}


TEST_CASE("Clear destroys all contained elements")
{
    Tracker::Reset();
    InlineRingBuffer<Tracker, 4> rb;
    rb.Push(1);
    rb.Push(2);
    rb.Push(3);
    REQUIRE(Tracker::Alive() == 3);

    rb.Clear();
    REQUIRE(rb.IsEmpty());
    REQUIRE(Tracker::Alive() == 0);
}


TEST_CASE("Destructor drains remaining elements")
{
    Tracker::Reset();
    {
        InlineRingBuffer<Tracker, 4> rb;
        rb.Push(1);
        rb.Push(2);
        REQUIRE(Tracker::Alive() == 2);
        // Intentionally leave elements in the buffer.
    }
    REQUIRE(Tracker::Alive() == 0);
}


TEST_CASE("Supports move-only types")
{
    InlineRingBuffer<std::unique_ptr<int>, 4> rb;

    rb.Push(std::make_unique<int>(42));
    rb.Push(std::make_unique<int>(43));

    REQUIRE(*rb.Peek() == 42);

    std::unique_ptr<int> p = rb.Pop();
    REQUIRE(*p == 42);
    REQUIRE(*rb.Pop() == 43);
    REQUIRE(rb.IsEmpty());
}


TEST_CASE("Capacity at index-type boundary wraps correctly")
{
    // Capacity * 2 - 1 = 399 does not fit in uint8_t, so the default index type must widen.
    // A too-narrow index would truncate readPos + size and corrupt the wrapped write position.
    constexpr size_t N = 200;
    InlineRingBuffer<int, N> rb;

    for (int i = 0; i < static_cast<int>(N); i++) {
        REQUIRE(rb.Push(i) != nullptr);
    }
    REQUIRE(rb.IsFull());

    // Drain half and refill to force the write position to wrap, then verify full FIFO integrity.
    for (int i = 0; i < 100; i++) {
        REQUIRE(rb.Pop() == i);
    }
    for (int i = 0; i < 100; i++) {
        REQUIRE(rb.Push(static_cast<int>(N) + i) != nullptr);
    }
    REQUIRE(rb.IsFull());

    for (int i = 100; i < static_cast<int>(N) + 100; i++) {
        REQUIRE(rb.Pop() == i);
    }
    REQUIRE(rb.IsEmpty());
}
