#include <pulse/config.h>

#ifdef pulseConfig_ENABLE_TIMER

#include <pulse/timer.h>
#include <pulse/heap.h>
#include <etl/variant.h>
#include <etl/atomic.h>


using namespace pulse;
using TickCount = Timer::TickCount;
using TimerEntry = details::TimerEntry;


static_assert(pulseConfig_TICK_FREQ > 0,
              "pulseConfig_TICK_FREQ must be positive");

static_assert(pulseConfig_MAX_TIMERS > 0,
              "pulseConfig_MAX_TIMERS must be positive");


struct pulse::details::TimerEntry {
    Timer::Handle timer;
    TickCount time;

    TimerEntry()
    {}

    TimerEntry(const TimerEntry &) = delete;
    TimerEntry(TimerEntry &&other) = default;

    TimerEntry(Timer::Handle timer, TickCount time):
        timer(etl::move(timer)),
        time(time)
    {}

    TimerEntry &
    operator =(TimerEntry &&other) = default;

    void
    Fire();

    void
    ReplaceTimer(Timer::Handle timer)
    {
        this->timer = etl::move(timer);
    }

    static bool
    IsBefore(const TimerEntry &a, const TimerEntry &b)
    {
        return Timer::IsBefore(a.time, b.time);
    }

    static void
    SetIndex(TimerEntry &e, size_t index)
    {
        e.timer->heapIdx = index;
    }
};


namespace {

etl::atomic<TickCount> curTime;

Heap<TimerEntry, details::TimerEntry::IsBefore, pulseConfig_MAX_TIMERS,
     details::TimerEntry::SetIndex> timers;

void
ScheduleTimer(Timer::Handle timer, TickCount time)
{
    if (!timers.Insert(TimerEntry(etl::move(timer), time))) {
        PULSE_PANIC("Out of timers capacity");
    }
}

} // anonymous namespace

void
TimerEntry::Fire()
{
    timer->Fire();
    timer.Reset();
}

TickCount
Timer::GetTime()
{
    return curTime;
}

TickCount
Timer::SetTime(TickCount time)
{
    TickCount ret = curTime;
    curTime = time;
    details::CheckTimers();
    return ret;
}

void
Timer::Tick()
{
    curTime++;
    // Timers fired from scheduler
}


Timer::Timer(TickCount expiresAt)
{
    Schedule(expiresAt);
}

Timer::Timer(Duration expiresAfter):
    Timer(GetTime() + expiresAfter.duration)
{}

Timer::Timer(Timer &&other):
    waiters(etl::move(other.waiters)),
    heapIdx(other.heapIdx),
    state(other.state)
{
    if (state == State::SCHEDULED) {
        timers.Item(heapIdx).ReplaceTimer(this);
        for (auto waiter: waiters) {
            waiter->timer = this;
        }
    }
    other.state = State::INITIAL;
}

Timer::~Timer()
{
    PULSE_ASSERT(refCounter == 0);
    if (state == SCHEDULED) {
        // May be called from Heap destructor when in unit tests. In this case it should not be
        // removed from timers ring. Reference counter is zero so there is definitely no external
        // links to this instance.
        state = INITIAL;
    }
    CancelImpl(true);
}

int
Timer::ExpiresAt(TickCount time)
{
    int ret = Cancel();
    Schedule(time);
    return ret;
}

int
Timer::ExpiresAfter(Duration delay)
{
    return ExpiresAt(GetTime() + delay.duration);
}

int
Timer::CancelImpl(bool force)
{
    State state = GetState();
    if ((!force && state != SCHEDULED) || (force && state != SCHEDULED && state != INITIAL)) {
        return 0;
    }
    if (state == SCHEDULED) {
        timers.Remove(heapIdx);
    }
    this->state = CANCELLED;
    return ScheduleAwaiters();
}

void
Timer::Schedule(TickCount time)
{
    PULSE_ASSERT(state != SCHEDULED);
    if (Timer::IsReached(curTime.load(), time)) {
        state = FIRED;
    } else {
        state = SCHEDULED;
        ScheduleTimer(this, time);
    }
}

void
Timer::Fire()
{
    state = FIRED;
    ScheduleAwaiters();
}

int
Timer::ScheduleAwaiters()
{
    State state = GetState();
    PULSE_ASSERT(state == FIRED || state == CANCELLED);
    int n = 0;
    while (true) {
        TimerAwaiter *awaiter = waiters.PopFirst();
        if (!awaiter) {
            break;
        }
        n++;
        awaiter->state = state == FIRED ?
            TimerAwaiter::State::FIRED : TimerAwaiter::State::CANCELLED;
        awaiter->timer.Reset();
        Task task = awaiter->waiter.Lock();
        awaiter->waiter.Reset();
        if (task) {
            etl::move(task).Schedule();
        }
    }
    return n;
}

TickCount
details::CheckTimers()
{
    while (!timers.IsEmpty()) {
        TimerEntry &e = timers.Top();
        if (Timer::IsAfter(e.time, curTime)) {
            return e.time - curTime;
        }
        e.Fire();
        timers.PopTop();
    }
    return etl::numeric_limits<TickCount>::max();
}


TimerAwaiter::TimerAwaiter(Timer::Handle timer)
{
    auto ts = timer->GetState();
    switch (ts) {
    case Timer::State::FIRED:
        state = State::FIRED;
        break;
    case Timer::State::CANCELLED:
        state = State::CANCELLED;
        break;
    default:
        state = State::SCHEDULED;
        timer->waiters.AddFirst(this);
        this->timer = etl::move(timer);
        break;
    }
}

TimerAwaiter::~TimerAwaiter()
{
    if (state == State::SCHEDULED) {
        PULSE_ASSERT(timer);
        timer->waiters.Remove(this);
    }
}

bool
TimerAwaiter::await_ready() const
{
    return state != State::SCHEDULED;
}

bool
TimerAwaiter::await_suspend(Task::CoroutineHandle handle)
{
    if (state != State::SCHEDULED) {
        return false;
    }
    waiter = Task(handle).GetWeakPtr();
    return true;
}

bool
TimerAwaiter::await_resume() const
{
    PULSE_ASSERT(state != State::SCHEDULED);
    return state == State::FIRED;
}

etl::optional<bool>
TimerAwaiter::GetResult() const
{
    if (state == State::SCHEDULED) {
        return etl::nullopt;
    }
    return state == State::FIRED;
}

void
PulseTimerTick()
{
    Timer::Tick();
}

#endif // pulseConfig_ENABLE_TIMER
