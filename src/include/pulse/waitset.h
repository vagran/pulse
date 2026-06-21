#ifndef PULSE_WAITSET_H
#define PULSE_WAITSET_H

#include <pulse/task.h>


namespace pulse {


class WaitsetAwaiter;


namespace details {

template <class TFactory>
using WaitsetFactoryAwaiterResult = decltype(etl::declval<TFactory>()().await_resume());

template <class TFactory>
constexpr bool
IsWaitsetVoidFactory()
{
    return etl::is_void_v<WaitsetFactoryAwaiterResult<TFactory>>;
}

template <typename TFactory>
struct WaitsetSlotBase {
    const TFactory factory;
    TaskRef handler;
    bool hasResult = false;

    template <typename TFactoryFrom>
    WaitsetSlotBase(TFactoryFrom &&factory):
        factory(etl::forward<TFactoryFrom>(factory))
    {}
};

template<typename F>
concept WaitsetHandlerCallback =
    requires(F f, size_t n)
{
    { f(n) } -> etl::same_as<void>;
};

template <typename TFactory, bool IsVoid = IsWaitsetVoidFactory<TFactory>()>
struct WaitsetSlot: WaitsetSlotBase<TFactory> {
    using TResult = WaitsetFactoryAwaiterResult<TFactory>;

    union {
        TResult result;
    };

    template <typename TFactoryFrom>
    WaitsetSlot(TFactoryFrom &&factory):
        WaitsetSlotBase<TFactory>(etl::forward<TFactoryFrom>(factory))
        // Prevent `result` from default construction here.
    {}

    ~WaitsetSlot()
    {
        ClearResult();
    }

    template <typename U>
    void
    SetResult(U &&from)
    {
        PULSE_ASSERT(!this->hasResult);
        this->hasResult = true;
        etl::construct_at(&result, etl::forward<U>(from));
    }

    void
    DestroyResult()
    {
        PULSE_ASSERT(this->hasResult);
        etl::destroy_at(&result);
        this->hasResult = false;
    }

    TResult &
    Result()
    {
        PULSE_ASSERT(this->hasResult);
        return result;
    }

    void
    ClearResult()
    {
        if (this->hasResult) {
            DestroyResult();
        }
    }

    template <WaitsetHandlerCallback TCallback>
    void
    SetupHandler(size_t index, TCallback cbk);
};

template <class TFactory>
struct WaitsetSlot<TFactory, true>:
    WaitsetSlotBase<TFactory> {

    using WaitsetSlotBase<TFactory>::WaitsetSlotBase;

    template <WaitsetHandlerCallback TCallback>
    void
    SetupHandler(size_t index, TCallback cbk);

    void
    ClearResult()
    {
        this->hasResult = false;
    }
};

template <size_t _slotIndex, typename... TFactory>
struct WaitsetSlots;

template <size_t _slotIndex>
struct WaitsetSlots<_slotIndex> {

    template <WaitsetHandlerCallback TCallback>
    void
    SetupHandler(TCallback cbk)
    {}

    template <size_t index>
    void
    PopResult()
    {
        static_assert(false, "Should not be reached");
    }

    template <size_t index>
    void
    Result()
    {
        static_assert(false, "Should not be reached");
    }

    template <size_t index>
    void
    ClearResult()
    {
        static_assert(false, "Should not be reached");
    }
};

template <size_t _slotIndex, typename TFactory, typename... TTail>
struct WaitsetSlots<_slotIndex, TFactory, TTail...> : WaitsetSlots<_slotIndex + 1, TTail...> {
    static constexpr size_t slotIndex = _slotIndex;
    using Base = WaitsetSlots<_slotIndex + 1, TTail...>;

    WaitsetSlot<TFactory> slot;

    template <typename TFactoryFrom, typename... TTailFrom>
    WaitsetSlots(TFactoryFrom &&factory, TTailFrom &&... tail):
        Base(etl::forward<TTailFrom>(tail)...),
        slot(etl::forward<TFactoryFrom>(factory))
    {}

    template <WaitsetHandlerCallback TCallback>
    void
    SetupHandler(TCallback cbk);

    template <size_t index>
    auto
    PopResult()
    {
        if constexpr (index == slotIndex) {
            auto result = etl::move(slot.Result());
            slot.DestroyResult();
            return result;
        } else {
            return Base::template PopResult<index>();
        }
    }

    template <size_t index>
    auto &
    Result()
    {
        if constexpr (index == slotIndex) {
            return slot.Result();
        } else {
            return Base::template Result<index>();
        }
    }

    template <size_t index>
    void
    ClearResult()
    {
        if constexpr (index == slotIndex) {
            slot.ClearResult();
        } else {
            Base::template ClearResult<index>();
        }
    }
};


class WaitsetBase {
public:
    /// Returned by awaiter if waitset destructed.
    static constexpr size_t DESTRUCTED = etl::numeric_limits<size_t>::max();

    WaitsetBase(const WaitsetBase &) = delete;
    WaitsetBase(WaitsetBase &&) = delete;

    /// Wait until any awaiter is ready. Awaiter returns index of the first ready awaiter, or
    /// DESTRUCTED in waitset destructed while being awaited.
    /// Note, that if awaited concurrently by multiple coroutines, multiple awaiters may receive the
    /// same ready slot index, so they should check if result available by HasResult() method. In
    /// general, it is not really intended to be used in such way.
    WaitsetAwaiter
    WaitAny();

protected:
    struct AwaiterSourceTrait;
    using TAwaiterBase = details::AwaiterBase<size_t, WaitsetBase, AwaiterSourceTrait>;

    friend class pulse::WaitsetAwaiter;

    struct AwaiterSourceTrait {
        static void
        DequeueAwaiter(WaitsetBase *ws, TAwaiterBase *awaiter);
    };

    WaitsetBase() = default;

    TailedList<TAwaiterBase *> waiters;
    uint32_t readyMask = 0;

    void
    Wakeup(TAwaiterBase *waiter, size_t result);

    virtual void
    PrepareWait() = 0;

    constexpr static auto
    SlotMask(size_t slotIndex)
    {
        return static_cast<decltype(readyMask)>(1) << slotIndex;
    }
};

} // namespace details


/**
 * This class addresses a race condition inherent to `tasks::WhenAny()`. Once `tasks::WhenAny()`
 * resumes, it selects a single ready awaitable and immediately destroys the remaining awaitables
 * (when provided as rvalues). If multiple awaitables become ready concurrently, the results of all
 * but the selected awaitable may be discarded.
 *
 * A similar issue can occur within an awaitable chain, where a lower-level operation has already
 * completed but its result has not yet propagated to the top-level coroutine when destruction
 * occurs. `Waitset` solves this problem by retaining completed awaitables and their results,
 * ensuring that no completion events are lost when processing awaitables in a loop.
 *
 * @tparam TFactory Types for awaiter factories.
 */
template <class... TFactory>
class Waitset: public details::WaitsetBase {
public:
    static constexpr size_t numSlots = sizeof...(TFactory);

    static_assert(numSlots <= sizeof(readyMask) * 8);

    template <typename... TFactoryFrom>
    Waitset(TFactoryFrom &&... factories):
        slots(etl::forward<TFactoryFrom>(factories)...)
    {}

    ~Waitset();

    inline WaitsetAwaiter
    operator co_await();

    /// @return True if corresponding slot has result set.
    bool
    HasResult(size_t index);

    /// Extract result from waitset slot. Any pending result will awake any future waits instantly
    /// until extracted. Precondition: corresponding slot must contain result.
    template <size_t index>
    auto
    PopResult()
    {
        readyMask &= ~SlotMask(index);
        return slots.template PopResult<index>();
    }

    /// Get reference to the result in the specified slot. Precondition: corresponding slot must
    /// contain result.
    template <size_t index>
    auto &
    Result()
    {
        return slots.template Result<index>();
    }

    /// Clear result in the specified slot if any set.
    template <size_t index>
    void
    ClearResult()
    {
        readyMask &= ~SlotMask(index);
        slots.template ClearResult<index>();
    }

private:
    friend class WaitsetAwaiter;

    details::WaitsetSlots<0, TFactory...> slots;

    void
    PrepareWait() override;
};


class WaitsetAwaiter: public details::WaitsetBase::TAwaiterBase {
public:
    bool
    await_suspend(tasks::CoroutineHandle handle);

private:
    friend details::WaitsetBase;
    using Base = details::WaitsetBase::TAwaiterBase;

    using Base::Base;
};

template <class... TFactory>
static auto
CreateWaitset(TFactory &&... factories)
{
    return Waitset<etl::remove_cvref_t<TFactory>...>(etl::forward<TFactory>(factories)...);
}


template <typename TFactory, bool IsVoid>
template <details::WaitsetHandlerCallback TCallback>
void
details::WaitsetSlot<TFactory, IsVoid>::SetupHandler(size_t index, TCallback cbk)
{
    if (this->handler || this->hasResult) {
        return;
    }

    auto handler = [](decltype(this) self, size_t index, TCallback cbk) -> Awaitable<void> {
        self->SetResult(co_await self->factory());
        self->handler.ReleaseHandle();
        cbk(index);
    }(this, index, cbk);

    if (!this->hasResult) {
        this->handler = handler;
    }
}


template <class TFactory>
template <details::WaitsetHandlerCallback TCallback>
void
details::WaitsetSlot<TFactory, true>::SetupHandler(size_t index, TCallback cbk)
{
    if (this->handler || this->hasResult) {
        return;
    }

    auto handler = [](decltype(this) self, size_t index, TCallback cbk) -> Awaitable<void> {
        co_await self->factory();
        self->hasResult = true;
        self->handler.ReleaseHandle();
        cbk(index);
    }(this, index, cbk);

    if (!this->hasResult) {
        this->handler = handler;
    }
}


template <size_t _slotIndex, typename TFactory, typename... TTail>
template <details::WaitsetHandlerCallback TCallback>
void
details::WaitsetSlots<_slotIndex, TFactory, TTail...>::SetupHandler(TCallback cbk)
{
    slot.SetupHandler(slotIndex, cbk);
    Base::SetupHandler(cbk);
}

template <class... TFactory>
Waitset<TFactory...>::~Waitset()
{
    for (auto waiter: waiters) {
        Wakeup(waiter, DESTRUCTED);
    }
}

template <class... TFactory>
WaitsetAwaiter
Waitset<TFactory...>::operator co_await()
{
    return WaitAny();
}

template <class... TFactory>
bool
Waitset<TFactory...>::HasResult(size_t index)
{
    PULSE_ASSERT(index < numSlots);
    return readyMask & SlotMask(index);
}

template <class... TFactory>
void
Waitset<TFactory...>::PrepareWait()
{
    slots.SetupHandler([this](size_t slotIndex){
        readyMask |= SlotMask(slotIndex);
        while (!waiters.IsEmpty()) {
            Wakeup(waiters.PopFirst(), slotIndex);
        }
    });
}

} // namespace pulse


#endif /* PULSE_WAITSET_H */
