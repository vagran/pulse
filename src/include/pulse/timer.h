#ifndef TIMER_H
#define TIMER_H

#ifdef __cplusplus

#include <pulse/details/common.h>
#include <pulse/config.h>
#include <pulse/shared_ptr.h>
#include <pulse/task.h>
#include <pulse/compare.h>
#include <etl/chrono.h>
#include <etl/optional.h>


#ifndef pulseConfig_TICK_FREQ
#error pulseConfig_TICK_FREQ must be defined in order to use timer API.
#endif


namespace pulse {


class DelayAwaiter;
class TimerAwaiter;

namespace details {

struct TimerEntry;

} // namespace details

class Timer {
public:
    using TickCount = uint32_t;

    /** Duration unit representing timer tick. */
    using Ticks = etl::chrono::duration<TickCount, etl::ratio<1, pulseConfig_TICK_FREQ>>;

    /** Tag type to differentiate between time point and duration expressed in ticks. */
    struct Duration {
        TickCount duration;

        Duration(TickCount duration):
            duration(duration)
        {}

        template <typename TRep, typename TPeriod>
        Duration(etl::chrono::duration<TRep, TPeriod> duration):
            duration(etl::chrono::duration_cast<Ticks>(duration).count())
        {}
    };

private:
    friend struct details::SharedPtrDefaultAtomicTrait<Timer>;

    struct SharedPtrTrait: details::SharedPtrDefaultAtomicTrait<Timer> {
        static void
        Delete(Timer &obj)
        {
            if (obj.dynamicAlloc) {
                delete &obj;
            }
        }
    };

public:
    using Handle = SharedPtr<Timer, SharedPtrTrait>;

    Timer() = default;

    /** Start timer expiring at the specified time. */
    Timer(TickCount expiresAt);

    /** Start timer expiring after the specified time. */
    Timer(Duration expiresAfter);

    Timer(const Timer &) = delete;

    /** Transfer state from another timer which becomes cancelled (all waiters are cancelled as
     * well).
     */
    Timer(Timer &&other);

    /** Create new timer instance. */
    template <typename... Args>
    static Timer::Handle
    Create(Args&&... args);

    ~Timer();

    /** Set expiration time relative to current time. Previous delay is cancelled if set.
     * @return Number of cancelled waiters.
     */
    int
    ExpiresAt(TickCount time);

    /** Set expiration time (absolute). Previous delay is cancelled if set.
     * @return Number of cancelled waiters.
     */
    int
    ExpiresAfter(Duration delay);

    /** Wait until timer is fired. Timer state is checked when co_await is applied to the returned
     * value. So if the timer was expired and set again between Wait() and co_await, it will waits
     * for newly set interval.
     * @note It could be implemented so that it is bound to state at the Wait() call moment, but
     * this would require dynamic allocation for shared awaiter object.
     * @return True if timer fired, false if cancelled. False is also returned immediately if timer
     * is not set.
     */
    inline TimerAwaiter
    Wait();

    inline TimerAwaiter
    operator co_await();

    /** Cancel current delay if any.
     * @return Number of cancelled waiters.
     */
    int
    Cancel()
    {
        return CancelImpl(false);
    }

    static constexpr bool
    IsBefore(TickCount t1, TickCount t2)
    {
        return static_cast<int32_t>(t1 - t2) < 0;
    }

    static constexpr bool
    IsAfter(TickCount t1, TickCount t2)
    {
        return static_cast<int32_t>(t1 - t2) > 0;
    }

    static constexpr bool
    IsReached(TickCount t1, TickCount t2)
    {
        return !IsBefore(t1, t2);
    }

    static TickCount
    GetTime();

    /** Set current time. May cause scheduled timers immediate firing if new time is past their
     * scheduled time.
     * @return Previous value of current time.
     */
    static TickCount
    SetTime(TickCount time);

    /** Call it (possibly from ISR) for every tick. Should be called with pulseConfig_TICK_FREQ
     * frequency. This call can be made from any priority ISR.
     */
    static void
    Tick();

    static inline Awaitable<void>
    Delay(Duration duration);

    static inline Awaitable<void>
    WaitUntil(TickCount time);

private:
    friend struct details::TimerEntry;
    friend class TimerAwaiter;

    enum State: uint8_t {
        INITIAL,
        SCHEDULED,
        FIRED,
        CANCELLED
    };

    List<TimerAwaiter *> waiters;
    // Index in heap when scheduled.
    SizedUint<etl::bit_width(static_cast<uintmax_t>(pulseConfig_MAX_TIMERS))> heapIdx;
    etl::atomic<uint8_t> refCounter = 0;
    uint8_t dynamicAlloc:1 = 0,
            state: 2 = INITIAL;

    /** @param force Cancel waiters in INITIAL state if true. */
    int
    CancelImpl(bool force);

    State
    GetState() const
    {
        return static_cast<State>(state);
    }

    bool
    IsReady() const
    {
        return state == FIRED || state == CANCELLED;
    }

    void
    Schedule(TickCount time);

    void
    Fire();

    /// @return Number of affected awaiters.
    int
    ScheduleAwaiters();
};


class TimerAwaiter {
public:
    ~TimerAwaiter();

    bool
    await_ready() const;

    bool
    await_suspend(Task::CoroutineHandle handle);

    bool
    await_resume() const;

    /** @return True if fired, false if cancelled, nullopt if not ready. */
    etl::optional<bool>
    GetResult() const;

private:
    friend class Timer;
    friend struct details::ListDefaultTrait<TimerAwaiter *>;

    enum class State: uint8_t {
        SCHEDULED,
        FIRED,
        CANCELLED
    };

    TimerAwaiter *next = nullptr;
    // Set to null when completes
    Timer::Handle timer;
    Task::WeakPtr waiter;
    State state;

    TimerAwaiter() = delete;
    TimerAwaiter(const TimerAwaiter &) = delete;
    TimerAwaiter(TimerAwaiter &&) = delete;

    TimerAwaiter(Timer::Handle timer);
};


namespace duration_await {

/// Equivalent of `co_await Timer::Delay()`.
/// Use it like this:
/// @code
/// using namespace etl::chrono_literal;
/// using namespace pulse::duration_await;
///
/// co_await 42_s;
/// @endcode
inline TaskAwaiter<void>
operator co_await(Timer::Duration duration)
{
    auto func = [](Timer::Duration duration) -> Awaitable<void> {
        Timer timer(duration);
        co_await timer;
    };
    return func(duration).Wait();
}

} // namespace duration_await


namespace details {

/** Fire all ready timers if any. Called by scheduler.
 * @return Time left until next timer. Maximal representable value if no timers.
 */
Timer::TickCount
CheckTimers();

} // namespace details


template <typename... Args>
Timer::Handle
Timer::Create(Args&&... args)
{
    Timer *p = new Timer(etl::forward<Args>(args)...);
    if (!p) {
        return nullptr;
    }
    p->dynamicAlloc = 1;
    return p;
}

Awaitable<void>
Timer::Delay(Duration duration)
{
    Timer timer(duration);
    co_await timer;
}

Awaitable<void>
Timer::WaitUntil(TickCount time)
{
    Timer timer(time);
    co_await timer;
}

TimerAwaiter
Timer::Wait()
{
    return TimerAwaiter(this);
}

TimerAwaiter
Timer::operator co_await()
{
    return Wait();
}

inline TimerAwaiter
operator co_await(const Timer::Handle &timer)
{
    return timer->Wait();
}

} // namespace pulse

extern "C" void
PulseTimerTick();

#else // __cplusplus

// Equivalent for pulse::Timer::Tick() for C.
void
PulseTimerTick();

#endif // __cplusplus

#endif /* TIMER_H */
