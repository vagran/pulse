#include <catch2/catch_test_macros.hpp>
#include <pulse/coroutine.h>
#include <concepts>


namespace {

template<typename T>
struct Generator
{
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::optional<T> value;

        // Promise default constructor call is not injected by the compiler if not defined
        // explicitly.
        promise_type() = default;

        Generator
        get_return_object()
        {
            return Generator(handle_type::from_promise(*this));
        }

        std::suspend_always
        initial_suspend()
        {
            return {};
        }

        std::suspend_always
        final_suspend() noexcept
        {
            return {};
        }

        void
        unhandled_exception()
        {}

        template<std::convertible_to<T> From>
        std::suspend_always
        yield_value(From&& from)
        {
            value.emplace(std::forward<From>(from));
            return {};
        }

        void
        return_void()
        {}
    };

    handle_type handle;

    Generator(handle_type handle):
        handle(handle)
    {}

    ~Generator()
    {
        handle.destroy();
    }

    explicit
    operator bool()
    {
        fill(); // The only way to reliably find out whether or not we finished coroutine,
                // whether or not there is going to be a next value generated (co_yield)
                // in coroutine via C++ getter (operator () below) is to execute/resume
                // coroutine until the next co_yield point (or let it fall off end).
                // Then we store/cache result in promise to allow getter (operator() below
                // to grab it without executing coroutine).
        return !handle.done();
    }

    T
    operator()()
    {
        fill();
        T value(std::move(*handle.promise().value));
        handle.promise().value.reset();
        return std::move(value);
    }

private:
    void
    fill()
    {
        if (!handle.promise().value) {
            handle.resume();
        }
    }
};

Generator<unsigned>
FibonacciSequence(unsigned n)
{
    if (n == 0) {
        co_return;
    }

    co_yield 0;

    if (n == 1) {
        co_return;
    }

    co_yield 1;

    if (n == 2) {
        co_return;
    }

    unsigned a = 0;
    unsigned b = 1;

    for (unsigned i = 2; i < n; ++i) {
        std::uint64_t s = a + b;
        co_yield s;
        a = b;
        b = s;
    }
}

} // anonymous namespace

TEST_CASE("Generator")
{
    auto gen = FibonacciSequence(8);
    REQUIRE(gen);
    REQUIRE(gen() == 0);
    REQUIRE(gen);
    REQUIRE(gen() == 1);
    REQUIRE(gen);
    REQUIRE(gen() == 1);
    REQUIRE(gen);
    REQUIRE(gen() == 2);
    REQUIRE(gen);
    REQUIRE(gen() == 3);
    REQUIRE(gen);
    REQUIRE(gen() == 5);
    REQUIRE(gen);
    REQUIRE(gen() == 8);
    REQUIRE(gen);
    REQUIRE(gen() == 13);
    REQUIRE(!gen);
}
