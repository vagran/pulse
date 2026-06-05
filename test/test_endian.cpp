#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <pulse/endian.h>
#include <etl/bit.h>


using namespace pulse;


TEST_CASE("ByteSwap uint8")
{
    REQUIRE(ByteSwap(uint8_t{0x12}) == 0x12);
}

TEST_CASE("ByteSwap uint16")
{
    REQUIRE(ByteSwap(uint16_t{0x1234}) == 0x3412);
}

TEST_CASE("ByteSwap uint32")
{
    REQUIRE(ByteSwap(uint32_t{0x12345678}) == 0x78563412);
}

TEST_CASE("ByteSwap uint64")
{
    REQUIRE(ByteSwap(uint64_t{0x1122334455667788ULL}) == 0x8877665544332211ULL);
}

TEST_CASE("ByteSwap signed integers")
{
    REQUIRE(ByteSwap(int16_t{0x1234}) == static_cast<int16_t>(0x3412));

    REQUIRE(ByteSwap(int32_t{0x12345678}) == static_cast<int32_t>(0x78563412));
}


TEST_CASE("ByteSwap float")
{
    constexpr float value = 1.0f;

    const uint32_t original = etl::bit_cast<uint32_t>(value);

    const float swapped = ByteSwap(value);

    const uint32_t swappedBits = etl::bit_cast<uint32_t>(swapped);

    REQUIRE(swappedBits == ByteSwap(original));
}


TEST_CASE("ByteSwap double")
{
    constexpr double value = 1.0;

    const uint64_t original = etl::bit_cast<uint64_t>(value);

    const double swapped = ByteSwap(value);

    const uint64_t swappedBits = etl::bit_cast<uint64_t>(swapped);

    REQUIRE(swappedBits == ByteSwap(original));
}


TEMPLATE_TEST_CASE(
    "ByteSwap twice returns original",
    "[endian]",
    uint8_t,
    uint16_t,
    uint32_t,
    uint64_t,
    int8_t,
    int16_t,
    int32_t,
    int64_t)
{
    const TestType value = static_cast<TestType>(0x12345678);

    REQUIRE(ByteSwap(ByteSwap(value)) == value);
}


TEST_CASE("ByteSwap float twice returns original")
{
    float value = 123.456f;

    REQUIRE(etl::bit_cast<uint32_t>(ByteSwap(ByteSwap(value))) == etl::bit_cast<uint32_t>(value));
}

TEST_CASE("ByteSwap double twice returns original")
{
    double value = 123.456;

    REQUIRE(etl::bit_cast<uint64_t>( ByteSwap(ByteSwap(value))) == etl::bit_cast<uint64_t>(value));
}


TEST_CASE("ByteSwap constexpr")
{
    constexpr uint32_t value = ByteSwap(uint32_t{0x12345678});

    static_assert(value == 0x78563412);

    REQUIRE(value == 0x78563412);
}


TEST_CASE("LeUint32 stores little endian bytes")
{
    LeUint32 value = 0x12345678;

    auto bytes = value.Bytes();

    if constexpr (IsSystemLe()) {
        REQUIRE(bytes[0] == 0x78);
        REQUIRE(bytes[1] == 0x56);
        REQUIRE(bytes[2] == 0x34);
        REQUIRE(bytes[3] == 0x12);
    } else {
        REQUIRE(bytes[0] == 0x78);
        REQUIRE(bytes[1] == 0x56);
        REQUIRE(bytes[2] == 0x34);
        REQUIRE(bytes[3] == 0x12);
    }
}


TEST_CASE("BeUint32 stores big endian bytes")
{
    BeUint32 value = 0x12345678;

    auto bytes = value.Bytes();

    REQUIRE(bytes[0] == 0x12);
    REQUIRE(bytes[1] == 0x34);
    REQUIRE(bytes[2] == 0x56);
    REQUIRE(bytes[3] == 0x78);
}


TEST_CASE("LeUint32 conversion")
{
    LeUint32 value = 0x12345678;

    uint32_t x = value;

    REQUIRE(x == 0x12345678);
}


TEST_CASE("LeUint32 Get Set")
{
    uint8_t buffer[sizeof(LeUint32)];

    LeUint32::Set(buffer, 0x12345678);

    uint32_t value = LeUint32::Get(buffer);

    REQUIRE(value == 0x12345678);
}


TEST_CASE("BeUint32 Get Set")
{
    uint8_t buffer[sizeof(BeUint32)];

    BeUint32::Set(buffer, 0x12345678);

    uint32_t value = BeUint32::Get(buffer);

    REQUIRE(value == 0x12345678);
}


TEST_CASE("Wire type sizes")
{
    static_assert(sizeof(LeUint8)  == sizeof(uint8_t));
    static_assert(sizeof(LeUint16) == sizeof(uint16_t));
    static_assert(sizeof(LeUint32) == sizeof(uint32_t));
    static_assert(sizeof(LeUint64) == sizeof(uint64_t));

    static_assert(sizeof(BeUint8)  == sizeof(uint8_t));
    static_assert(sizeof(BeUint16) == sizeof(uint16_t));
    static_assert(sizeof(BeUint32) == sizeof(uint32_t));
    static_assert(sizeof(BeUint64) == sizeof(uint64_t));

    SUCCEED();
}
