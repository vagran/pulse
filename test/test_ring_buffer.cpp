#include <catch2/catch_test_macros.hpp>
#include <pulse/ring_buffer.h>

using namespace pulse;


TEST_CASE("RingBuffer basic push/pop")
{
    RingBuffer<int, 8> rb;

    int input[] = {1, 2, 3, 4};
    int output[4] = {};

    REQUIRE(rb.capacity == 8);
    REQUIRE(rb.GetSize() == 0);

    auto pushed = rb.Write(input, 4);
    REQUIRE(pushed == 4);
    REQUIRE(rb.GetSize() == 4);

    auto popped = rb.Read(output, 4);
    REQUIRE(popped == 4);
    REQUIRE(rb.GetSize() == 0);

    REQUIRE(std::equal(output, output + 4, input));
}

TEST_CASE("RingBuffer basic push/pop (external storage)")
{
    int buffer[8];
    RingBuffer<int> rb(buffer, PULSE_SIZEOF_ARRAY(buffer));

    int input[] = {1, 2, 3, 4};
    int output[4] = {};

    REQUIRE(rb.capacity == 8);
    REQUIRE(rb.GetSize() == 0);

    auto pushed = rb.Write(input, 4);
    REQUIRE(pushed == 4);
    REQUIRE(rb.GetSize() == 4);

    auto popped = rb.Read(output, 4);
    REQUIRE(popped == 4);
    REQUIRE(rb.GetSize() == 0);

    REQUIRE(std::equal(output, output + 4, input));
}

TEST_CASE("RingBuffer capacity limit")
{
    RingBuffer<int, 8> rb;

    int input[10] = {};

    auto pushed = rb.Write(input, 10);
    REQUIRE(pushed == 8);
    REQUIRE(rb.GetSize() == 8);
    REQUIRE(rb.GetAvailableCapacity() == 0);
}

TEST_CASE("RingBuffer wraparound behavior")
{
    RingBuffer<int, 8> rb;

    int input1[] = {1,2,3,4,5};
    int input2[] = {6,7,8};
    int output[8] = {};

    rb.Write(input1, 5);
    rb.Read(output, 3); // advance readPos

    rb.Write(input2, 3); // should wrap

    REQUIRE(rb.GetSize() == 5);

    rb.Read(output, 5);

    int expected[] = {4,5,6,7,8};
    REQUIRE(std::equal(output, output + 5, expected));
}

TEST_CASE("RingBuffer partial pop")
{
    RingBuffer<int, 8> rb;

    int input[] = {1,2,3};
    int output[5] = {};

    rb.Write(input, 3);

    auto popped = rb.Read(output, 5);
    REQUIRE(popped == 3);
    REQUIRE(rb.GetSize() == 0);

    REQUIRE(std::equal(output, output + 3, input));
}

TEST_CASE("RingBuffer zero-copy write region")
{
    RingBuffer<int, 8> rb;

    auto region = rb.GetWriteRegion();
    REQUIRE(region.size() == 8);

    for (size_t i = 0; i < region.size(); ++i)
        region[i] = static_cast<int>(i + 1);

    rb.CommitWrite(8);

    REQUIRE(rb.GetSize() == 8);
}

TEST_CASE("RingBuffer zero-copy read region")
{
    RingBuffer<int, 8> rb;

    int input[] = {1,2,3,4};
    rb.Write(input, 4);

    auto region = rb.GetReadRegion();
    REQUIRE(region.size() == 4);

    REQUIRE(region[0] == 1);
    REQUIRE(region[3] == 4);

    rb.CommitRead(4);
    REQUIRE(rb.GetSize() == 0);
}

TEST_CASE("RingBuffer zero-copy wrap split")
{
    RingBuffer<int, 8> rb;

    int input1[] = {1,2,3,4,5};
    rb.Write(input1, 5);

    int tmp[3];
    rb.Read(tmp, 3); // force wrap scenario

    auto region = rb.GetWriteRegion();

    // Should only expose tail until end of buffer
    REQUIRE(region.size() == 3);

    for (size_t i = 0; i < region.size(); ++i)
        region[i] = static_cast<int>(10 + i);

    rb.CommitWrite(region.size());

    REQUIRE(rb.GetSize() == 5);
}

TEST_CASE("RingBuffer zero-copy read wrap split", "[single]")
{
    RingBuffer<int, 8> rb;

    int input1[] = {1,2,3,4,5};
    int input2[] = {6,7,8, 9,10,11};

    rb.Write(input1, 5);

    int tmp[3];
    rb.Read(tmp, 3); // advance readPos

    rb.Write(input2, 6); // wrap write

    auto region = rb.GetReadRegion();

    // Should read only contiguous part
    REQUIRE(region.size() == 5);
    REQUIRE(std::equal(region.begin(), region.end(), std::vector<int>{4, 5, 6, 7, 8}.begin()));
    REQUIRE(region[0] == 4);
    REQUIRE(region[1] == 5);

    rb.CommitRead(region.size());

    REQUIRE(rb.GetSize() == 3);
}

TEST_CASE("RingBuffer multiple region iteration")
{
    RingBuffer<int, 8> rb;

    int input[] = {1,2,3,4,5,6};
    rb.Write(input, 6);

    int output[6];
    size_t offset = 0;

    while (rb.GetSize() > 0) {
        auto region = rb.GetReadRegion();
        std::copy(region.begin(), region.end(), output + offset);
        offset += region.size();
        rb.CommitRead(region.size());
    }

    REQUIRE(offset == 6);
    REQUIRE(std::equal(output, output + 6, input));
}

TEST_CASE("RingBuffer interleaved push/pop")
{
    RingBuffer<int, 8> rb;

    for (int i = 0; i < 100; ++i) {
        rb.Write(&i, 1);

        int out;
        rb.Read(&out, 1);

        REQUIRE(out == i);
    }

    REQUIRE(rb.GetSize() == 0);
}
