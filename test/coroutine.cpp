#include <catch2/catch_test_macros.hpp>
#include <pulse/coroutine.h>
#include <pulse/malloc.h>
#include <concepts>
#include <iostream>


namespace {

template<typename T>
struct Generator
{
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        T value_;

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
            value_ = std::forward<From>(from);
            return {};
        }

        void
        return_void()
        {}

        void *
        operator new(std::size_t n) noexcept
        {
            void *p = pulse::Malloc(n);
            std::cout << "Frame: " << p << "\n";
            return p;
        }

        void
        operator delete(void *p) noexcept
        {
            return pulse::Free(p);
        }
    };

    handle_type h_;

    Generator(handle_type h):
        h_(h)
    {}

    ~Generator()
    {
        h_.destroy();
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
        return !h_.done();
    }

    T
    operator()()
    {
        fill();
        full_ = false; // we are going to move out previously cached
                       // result to make promise empty again
        return std::move(h_.promise().value_);
    }

private:
    bool full_ = false;

    void
    fill()
    {
        if (!full_)
        {
            h_.resume();
            full_ = true;
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
