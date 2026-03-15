#ifndef COROUTINE_H
#define COROUTINE_H

#include <etl/concepts.h>


/** Stuff from std required for compiler coroutines support. */
namespace std {

namespace details {

template<class, class...>
struct coroutine_traits_base
{};

template<class R, class... Args>
requires requires { typename R::promise_type; }
struct coroutine_traits_base <R, Args...> {
    using promise_type = R::promise_type;
};

} // namespace details

template<class R, class... Args>
struct coroutine_traits: details::coroutine_traits_base<R, Args...>
{};

namespace details {


} // namespace details

template<typename Promise = void>
struct coroutine_handle;

template<>
struct coroutine_handle<void> {
    void *framePtr;

    coroutine_handle():
        framePtr(nullptr)
    {}

    explicit coroutine_handle(void *framePtr):
        framePtr(framePtr)
    {}

    operator bool() const
    {
        return framePtr != nullptr;
    }

    void
    resume() const
    {
        __builtin_coro_resume(framePtr);
    }

    void
    destroy() const
    {
        __builtin_coro_destroy(framePtr);
    }

    bool
    done() const
    {
        return __builtin_coro_done(framePtr);
    }
};

template<typename Promise>
struct coroutine_handle: coroutine_handle<void> {

    using coroutine_handle<void>::coroutine_handle;

    template <class TFromPromise>
    requires etl::derived_from<TFromPromise, Promise>
    coroutine_handle(const coroutine_handle<TFromPromise> &from):
        coroutine_handle(from.framePtr)
    {}

    Promise &
    promise() const
    {
        return *reinterpret_cast<Promise *>(__builtin_coro_promise(framePtr, 0, false));
    }

    static coroutine_handle
    from_address(void *framePtr)
    {
        return coroutine_handle(framePtr);
    }

    static coroutine_handle
    from_promise(Promise &p)
    {
        return coroutine_handle(__builtin_coro_promise(&p, 0, true));
    }
};


/** Helper for dummy awaiter which causes current coroutine to suspend. */
struct suspend_always {
    bool
    await_ready() const noexcept
    {
        return false;
    }

    void
    await_suspend(coroutine_handle<>) const noexcept
    {}

    void
    await_resume() const noexcept
    {}
};

/** Helper for dummy awaiter which does not causes current coroutine to suspend. */
struct suspend_never {
    bool
    await_ready() const noexcept
    {
        return true;
    }

    void
    await_suspend(coroutine_handle<>) const noexcept
    {}

    void
    await_resume() const noexcept
    {}
};

} // namespace std


#endif /* COROUTINE_H */
