#ifndef GENERATOR_H
#define GENERATOR_H

#include <pulse/coroutine.h>
#include <pulse/details/common.h>
#include <etl/optional.h>


namespace pulse {

template <typename T>
class GeneratorIterator;

/**
 * Synchronous single-pass destructive pull generator.
 *
 * @tparam T Yielded value type.
 */
template <typename T>
class Generator {
public:
    struct Promise;

    using TPromise = Promise;

    using CoroutineHandle = std::coroutine_handle<Promise>;

    struct Promise {
        etl::optional<T> value;

        Promise() = default;

        Generator<T>
        get_return_object()
        {
            return CoroutineHandle::from_promise(*this);
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

        template<etl::convertible_to<T> From>
        std::suspend_always
        yield_value(From&& from)
        {
            value.emplace(etl::forward<From>(from));
            return {};
        }

        void
        return_void()
        {}

        void
        unhandled_exception()
        {
            PULSE_PANIC("Generator::Promise::unhandled_exception");
        }
    };


    Generator(const Generator &) = delete;

    Generator(CoroutineHandle handle):
        handle(handle)
    {}

    Generator(Generator &&other):
        handle(other.handle)
    {
        other.handle = CoroutineHandle();
    }

    ~Generator()
    {
        if (handle) {
            handle.destroy();
        }
    }

    Generator &
    operator =(Generator&& other)
    {
        if (this != &other) {
            if (handle) {
                handle.destroy();
            }
            handle = other.handle;
            other.handle = CoroutineHandle();
        }
        return *this;
    }

    /** @return True if has next value, false if if sequence complete. */
    bool
    HasNext()
    {
        return Fill();
    }

    /** @return True if has next value, false if if sequence complete. */
    explicit operator bool()
    {
        return HasNext();
    }

    /** Get and consume next value. Should only be called when next value availability was
     * previously checked by `HasNext()` method. Use `TryNext()` method if need to get value without
     * check.
     */
    T
    Next()
    {
        if (!Fill()) {
            PULSE_PANIC("Generator invoked without next value");
        }
        Promise &promise = handle.promise();
        T result = etl::move(*promise.value);
        promise.value.reset();
        return result;
    }

    /** Get and consume next value. Should only be called when next value availability was
     * previously checked by `HasNext()` method. Use `TryNext()` method if need to get value without
     * check.
     */
    T
    operator()()
    {
        return Next();
    }

    /** Try get next value if available.
     * @return Next value, `nullopt` if sequence complete.
     */
    etl::optional<T>
    TryNext()
    {
        if (!Fill()) {
            return etl::nullopt;
        }
        Promise &promise = handle.promise();
        T result = etl::move(*promise.value);
        promise.value.reset();
        return result;
    }

    /** Get reference to current value without consuming it. Value availability should first be
     * checked by `HasNext()` method.
     */
    T &
    Current()
    {
        PULSE_ASSERT(handle);
        Promise &promise = handle.promise();
        if (!promise.value) {
            PULSE_PANIC("Finished generator current value accessed");
        }
        return *promise.value;
    }

    /** Consume current value if any. */
    void
    Consume()
    {
        if (!handle) {
            return;
        }
        Promise &promise = handle.promise();
        promise.value.reset();
    }

    inline GeneratorIterator<T>
    begin();

    inline GeneratorIterator<T>
    end();

private:
    CoroutineHandle handle;

    /// @return true if new value available.
    bool
    Fill()
    {
        if (!handle) {
            return false;
        }
        Promise &promise = handle.promise();
        while (true) {
            if (promise.value) {
                return true;
            }
            if (handle.done()) {
                return false;
            }
            handle.resume();
        }
    }
};


template <typename T>
class GeneratorIterator {
public:
    GeneratorIterator(const GeneratorIterator&) = delete;

    GeneratorIterator(GeneratorIterator &&other):
        gen(other.gen)
    {
        other.gen = nullptr;
    }

    T &
    operator*() const
    {
        PULSE_ASSERT(gen);
        return gen->Current();
    }

    T *
    operator->() const
    {
        return &(**this);
    }

    bool
    operator ==(const GeneratorIterator<T>& other) const
    {
        return gen == other.gen;
    }

    bool
    operator !=(const GeneratorIterator<T>& other) const
    {
        return gen != other.gen;
    }

    GeneratorIterator &
    operator++()
    {
        PULSE_ASSERT(gen);
        gen->Consume();
        if (!gen->HasNext()) {
            gen = nullptr;
        }
        return *this;
    }

private:
    friend class Generator<T>;

    Generator<T> *gen;

    GeneratorIterator():
        gen(nullptr)
    {}

    GeneratorIterator(Generator<T> *gen)
    {
        if (!gen->HasNext()) {
            this->gen = nullptr;
        } else {
            this->gen = gen;
        }
    }
};


template <typename T>
GeneratorIterator<T>
Generator<T>::begin()
{
    return GeneratorIterator<T>(this);
}

template <typename T>
GeneratorIterator<T>
Generator<T>::end()
{
    return GeneratorIterator<T>();
}

} // namespace pulse

// Bind GeneratorPromise to Generator coroutine type.
template<typename T, typename... Args>
struct std::coroutine_traits<pulse::Generator<T>, Args...> {
    using promise_type = typename pulse::Generator<T>::TPromise;
};


#endif /* GENERATOR_H */
