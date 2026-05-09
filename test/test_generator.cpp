#include <common.h>
#include <catch2/catch_test_macros.hpp>
#include <pulse/generator.h>


using pulse::Generator;


namespace {

Generator<int>
GenerateEmpty()
{
    co_return;
}

Generator<int>
GenerateOne()
{
    co_yield 42;
}

Generator<int>
GenerateSequence(int count)
{
    for (int i = 0; i < count; ++i) {
        co_yield i;
    }
}

Generator<int>
GenerateNested()
{
    co_yield 1;
    co_yield 2;
    co_yield 3;
}

class MoveOnly {
public:
    int value;

    explicit MoveOnly(int value):
        value(value)
    {}

    MoveOnly(const MoveOnly&) = delete;

    MoveOnly(MoveOnly&& other) noexcept:
        value(other.value)
    {
        other.value = -1;
    }
};

Generator<MoveOnly>
GenerateMoveOnlyOne()
{
    co_yield MoveOnly(42);
}

Generator<MoveOnly>
GenerateMoveOnlySequence()
{
    co_yield MoveOnly(10);
    co_yield MoveOnly(20);
    co_yield MoveOnly(30);
}

Generator<MoveOnly>
GenerateMoveOnlyEmpty()
{
    co_return;
}

} // anonymous namespace


TEST_CASE("Generator empty sequence")
{
    auto gen = GenerateEmpty();

    REQUIRE_FALSE(gen.HasNext());
    REQUIRE_FALSE(gen);
    REQUIRE_FALSE(gen.TryNext().has_value());
}


TEST_CASE("Generator single value")
{
    auto gen = GenerateOne();

    REQUIRE(gen.HasNext());
    REQUIRE(gen.Current() == 42);

    REQUIRE(gen.Next() == 42);

    REQUIRE_FALSE(gen.HasNext());
    REQUIRE_FALSE(gen.TryNext().has_value());
}


TEST_CASE("Generator sequence with Next")
{
    auto gen = GenerateSequence(5);

    for (int expected = 0; expected < 5; ++expected) {
        REQUIRE(gen.HasNext());
        REQUIRE(gen.Current() == expected);
        REQUIRE(gen.Next() == expected);
    }

    REQUIRE_FALSE(gen.HasNext());
}


TEST_CASE("Generator TryNext sequence")
{
    auto gen = GenerateSequence(4);

    for (int expected = 0; expected < 4; ++expected) {
        auto value = gen.TryNext();

        REQUIRE(value.has_value());
        REQUIRE(*value == expected);
    }

    REQUIRE_FALSE(gen.TryNext().has_value());
}


TEST_CASE("Generator operator() sequence")
{
    auto gen = GenerateSequence(3);

    REQUIRE(gen.HasNext());
    REQUIRE(gen() == 0);

    REQUIRE(gen.HasNext());
    REQUIRE(gen() == 1);

    REQUIRE(gen.HasNext());
    REQUIRE(gen() == 2);

    REQUIRE_FALSE(gen.HasNext());
}


TEST_CASE("Generator Current does not consume")
{
    auto gen = GenerateOne();

    REQUIRE(gen.HasNext());

    REQUIRE(gen.Current() == 42);
    REQUIRE(gen.Current() == 42);
    REQUIRE(gen.Current() == 42);

    REQUIRE(gen.Next() == 42);

    REQUIRE_FALSE(gen.HasNext());
}


TEST_CASE("Generator Consume skips current")
{
    auto gen = GenerateSequence(3);

    REQUIRE(gen.HasNext());
    REQUIRE(gen.Current() == 0);

    gen.Consume();

    REQUIRE(gen.HasNext());
    REQUIRE(gen.Current() == 1);

    REQUIRE(gen.Next() == 1);

    REQUIRE(gen.HasNext());
    REQUIRE(gen.Next() == 2);

    REQUIRE_FALSE(gen.HasNext());
}


TEST_CASE("Generator range-for iteration")
{
    auto gen = GenerateSequence(5);

    int expected = 0;

    for (auto value : gen) {
        REQUIRE(value == expected);
        ++expected;
    }

    REQUIRE(expected == 5);
}


TEST_CASE("Generator range-for reference iteration")
{
    auto gen = GenerateSequence(4);

    int expected = 0;

    for (const auto& value : gen) {
        REQUIRE(value == expected);
        ++expected;
    }

    REQUIRE(expected == 4);
}


TEST_CASE("Generator empty begin/end")
{
    auto gen = GenerateEmpty();

    auto begin = gen.begin();
    auto end = gen.end();

    REQUIRE(begin == end);
}


TEST_CASE("Generator move construction")
{
    auto original = GenerateSequence(3);

    REQUIRE(original.HasNext());
    REQUIRE(original.Current() == 0);

    auto moved = etl::move(original);

    REQUIRE_FALSE(original.HasNext());
    REQUIRE(moved.HasNext());
    REQUIRE(moved.Next() == 0);
    REQUIRE(moved.HasNext());
    REQUIRE(moved.Next() == 1);
    REQUIRE(moved.HasNext());
    REQUIRE(moved.Next() == 2);

    REQUIRE_FALSE(moved.HasNext());
}


TEST_CASE("Generator move assignment")
{
    auto source = GenerateSequence(4);
    auto destination = GenerateEmpty();

    destination = etl::move(source);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(destination.HasNext());
        REQUIRE(destination.Next() == i);
    }

    REQUIRE_FALSE(destination.HasNext());
}


TEST_CASE("Generator iterator manual traversal")
{
    auto gen = GenerateSequence(3);

    auto it = gen.begin();
    auto end = gen.end();

    REQUIRE(it != end);
    REQUIRE(*it == 0);

    ++it;
    REQUIRE(it != end);
    REQUIRE(*it == 1);

    ++it;
    REQUIRE(it != end);
    REQUIRE(*it == 2);

    ++it;
    REQUIRE(it == end);
}


TEST_CASE("Generator explicit sequence values")
{
    auto gen = GenerateNested();

    REQUIRE(gen.HasNext());
    REQUIRE(gen.Next() == 1);
    REQUIRE(gen.HasNext());
    REQUIRE(gen.Next() == 2);
    REQUIRE(gen.HasNext());
    REQUIRE(gen.Next() == 3);

    REQUIRE_FALSE(gen.HasNext());
}


TEST_CASE("Generator HasNext is idempotent until consumed")
{
    auto gen = GenerateSequence(2);

    REQUIRE(gen.HasNext());
    REQUIRE(gen.HasNext());
    REQUIRE(gen.HasNext());

    REQUIRE(gen.Current() == 0);
    REQUIRE(gen.Next() == 0);

    REQUIRE(gen.HasNext());
    REQUIRE(gen.HasNext());

    REQUIRE(gen.Next() == 1);

    REQUIRE_FALSE(gen.HasNext());
}


TEST_CASE("Generator exhaustion remains stable")
{
    auto gen = GenerateOne();

    REQUIRE(gen.HasNext());
    REQUIRE(gen.Next() == 42);

    REQUIRE_FALSE(gen.HasNext());
    REQUIRE_FALSE(gen.HasNext());
    REQUIRE_FALSE(gen.TryNext().has_value());
    REQUIRE_FALSE(gen.TryNext().has_value());
}


TEST_CASE("Generator supports single move-only value")
{
    auto gen = GenerateMoveOnlyOne();

    REQUIRE(gen.HasNext());

    auto value = gen.Next();

    REQUIRE(value.value == 42);

    REQUIRE_FALSE(gen.HasNext());
}


TEST_CASE("Generator TryNext supports move-only values")
{
    auto gen = GenerateMoveOnlySequence();

    {
        auto value = gen.TryNext();
        REQUIRE(value.has_value());
        REQUIRE(value->value == 10);
    }

    {
        auto value = gen.TryNext();
        REQUIRE(value.has_value());
        REQUIRE(value->value == 20);
    }

    {
        auto value = gen.TryNext();
        REQUIRE(value.has_value());
        REQUIRE(value->value == 30);
    }

    REQUIRE_FALSE(gen.TryNext().has_value());
}


TEST_CASE("Generator Current works with move-only values")
{
    auto gen = GenerateMoveOnlySequence();

    REQUIRE(gen.HasNext());
    REQUIRE(gen.Current().value == 10);

    auto first = gen.Next();
    REQUIRE(first.value == 10);

    REQUIRE(gen.HasNext());
    REQUIRE(gen.Current().value == 20);

    auto second = gen.Next();
    REQUIRE(second.value == 20);

    REQUIRE(gen.HasNext());
    REQUIRE(gen.Current().value == 30);

    auto third = gen.Next();
    REQUIRE(third.value == 30);

    REQUIRE_FALSE(gen.HasNext());
}


TEST_CASE("Generator Consume skips move-only value")
{
    auto gen = GenerateMoveOnlySequence();

    REQUIRE(gen.HasNext());
    REQUIRE(gen.Current().value == 10);

    gen.Consume();

    REQUIRE(gen.HasNext());
    REQUIRE(gen.Current().value == 20);

    auto second = gen.Next();
    REQUIRE(second.value == 20);

    auto third = gen.Next();
    REQUIRE(third.value == 30);

    REQUIRE_FALSE(gen.HasNext());
}


TEST_CASE("Generator operator() supports move-only values")
{
    auto gen = GenerateMoveOnlySequence();

    REQUIRE(gen.HasNext());
    auto first = gen();
    REQUIRE(first.value == 10);

    REQUIRE(gen.HasNext());
    auto second = gen();
    REQUIRE(second.value == 20);

    REQUIRE(gen.HasNext());
    auto third = gen();
    REQUIRE(third.value == 30);

    REQUIRE_FALSE(gen.HasNext());
}


TEST_CASE("Generator range-for supports move-only by reference")
{
    auto gen = GenerateMoveOnlySequence();

    int expected = 10;

    for (auto& value : gen) {
        REQUIRE(value.value == expected);
        expected += 10;
    }

    REQUIRE(expected == 40);
}


TEST_CASE("Generator iterator manually traverses move-only values")
{
    auto gen = GenerateMoveOnlySequence();

    auto it = gen.begin();
    auto end = gen.end();

    REQUIRE(it != end);
    REQUIRE(it->value == 10);

    ++it;
    REQUIRE(it != end);
    REQUIRE(it->value == 20);

    ++it;
    REQUIRE(it != end);
    REQUIRE(it->value == 30);

    ++it;
    REQUIRE(it == end);
}


TEST_CASE("Generator supports empty move-only generator")
{
    auto gen = GenerateMoveOnlyEmpty();

    REQUIRE_FALSE(gen.HasNext());
    REQUIRE_FALSE(gen.TryNext().has_value());

    auto begin = gen.begin();
    auto end = gen.end();

    REQUIRE(begin == end);
}


TEST_CASE("Generator move construction preserves move-only sequence")
{
    auto original = GenerateMoveOnlySequence();

    REQUIRE(original.HasNext());
    REQUIRE(original.Current().value == 10);

    auto moved = etl::move(original);

    REQUIRE(moved.HasNext());

    auto first = moved.Next();
    REQUIRE(first.value == 10);

    auto second = moved.Next();
    REQUIRE(second.value == 20);

    auto third = moved.Next();
    REQUIRE(third.value == 30);

    REQUIRE_FALSE(moved.HasNext());
}


TEST_CASE("Generator move assignment preserves move-only sequence")
{
    auto source = GenerateMoveOnlySequence();
    auto destination = GenerateMoveOnlyEmpty();

    destination = etl::move(source);

    auto first = destination.Next();
    REQUIRE(first.value == 10);

    auto second = destination.Next();
    REQUIRE(second.value == 20);

    auto third = destination.Next();
    REQUIRE(third.value == 30);

    REQUIRE_FALSE(destination.HasNext());
}


TEST_CASE("Generator HasNext stable for move-only values")
{
    auto gen = GenerateMoveOnlyOne();

    REQUIRE(gen.HasNext());
    REQUIRE(gen.HasNext());
    REQUIRE(gen.Current().value == 42);

    auto value = gen.Next();

    REQUIRE(value.value == 42);

    REQUIRE_FALSE(gen.HasNext());
    REQUIRE_FALSE(gen.HasNext());
}


TEST_CASE("Generator yielded move-only object transfers correctly")
{
    auto gen = GenerateMoveOnlyOne();

    REQUIRE(gen.HasNext());

    MoveOnly value = gen.Next();

    REQUIRE(value.value == 42);

    REQUIRE_FALSE(gen.HasNext());
}
