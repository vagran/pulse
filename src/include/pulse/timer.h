#ifndef TIMER_H
#define TIMER_H

#include <pulse/details/common.h>
#include <pulse/details/default_config.h>
#include <pulse/shared_ptr.h>
#include <pulse/task.h>
#include <etl/chrono.h>


#ifndef pulseConfig_TICK_FREQ
#error pulseConfig_TICK_FREQ must be defined in order to use timer API.
#endif


namespace pulse {


class DelayAwaiter;
class TimerAwaiter;

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
    friend struct details::SharedPtrDefaultTrait<Timer>;

    struct SharedPtrTrait: details::SharedPtrDefaultTrait<Timer> {
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

    Timer();

    /** Start timer expiring at the specified time. */
    Timer(TickCount expiresAt);

    /** Start timer expiring after the specified time. */
    Timer(Duration expiresAfter);

    Timer(const Timer &) = delete;

    /** Transfer state from another timer which becomes unset. */
    Timer(Timer &&other) noexcept;

    /** Create new timer instance. */
    template <typename... Args>
    static Timer::Handle
    Create(Args&&... args);

    //XXX cancel, check ref counter
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

    /** Wait until timer is fired.
     * @return True if timer fired, false if cancelled. False is also returned immediately if timer
     * is not set.
     */
    TimerAwaiter
    Wait();

    /** Cancel current delay if any.
     * @return Number of cancelled waiters.
     */
    int
    Cancel();

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

    static TickCount
    GetTime();

    /** Set current time. May cause scheduled timers immediate firing if new time is past their
     * scheduled time.
     * @return Previous value of current time.
     */
    static TickCount
    SetTime(TickCount time);

    /** Call it (possibly from ISR) for every tick. Should be called with pulseConfig_TICK_FREQ
     * frequency.
     */
    static void
    Tick();

    static inline DelayAwaiter
    Delay(Duration duration);

    static inline DelayAwaiter
    WaitUntil(TickCount time);

private:
    details::ListWeak<TimerAwaiter *> awaiters;
    uint8_t refCounter = 0;
    bool dynamicAlloc = false;
};

class DelayAwaiter {
public:
    const Timer::TickCount time;

    bool
    await_ready() const;

    void
    await_suspend(Task::CoroutineHandle handle);

    void
    await_resume() const
    {}
};

class TimerAwaiter {
public:
    TimerAwaiter *next = nullptr;
    //XXX
};

namespace details {

/** Fire all ready timers if any. Called by scheduler.
 * @return True if some timers fired.
 */
bool
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
    p->dynamicAlloc = true;
    return p;
}

DelayAwaiter
Timer::Delay(Duration duration)
{
    return DelayAwaiter{Timer::GetTime() + duration.duration};
}

DelayAwaiter
Timer::WaitUntil(TickCount time)
{
    return DelayAwaiter{time};
}

} // namespace pulse

#endif /* TIMER_H */
