#include <pulse/details/default_config.h>

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
    //XXX do not use etl::variant due to big overhead
    etl::variant<etl::monostate, Task, Timer::Handle> data;
    TickCount time;

    void
    Fire();

    void
    ReplaceTimer(Timer::Handle timer)
    {
        PULSE_ASSERT(etl::holds_alternative<Timer::Handle>(data));
        etl::get<Timer::Handle>(data) = etl::move(timer);
    }

    static bool
    IsBefore(const TimerEntry &a, const TimerEntry &b)
    {
        return Timer::IsBefore(a.time, b.time);
    }

    static void
    SetIndex(TimerEntry &e, size_t index)
    {
        if (etl::holds_alternative<Timer::Handle>(e.data)) {
            etl::get<Timer::Handle>(e.data)->heapIdx = index;
        }
    }
};


namespace {

etl::atomic<TickCount> curTime;

Heap<TimerEntry, details::TimerEntry::IsBefore, pulseConfig_MAX_TIMERS,
     details::TimerEntry::SetIndex> timers;

void
ScheduleTask(Task task, TickCount time)
{
    if (!timers.Insert(TimerEntry{etl::move(task), time})) {
        PULSE_PANIC("Out of timers capacity");
    }
}

void
ScheduleTimer(Timer::Handle timer, TickCount time)
{
    if (!timers.Insert(TimerEntry{etl::move(timer), time})) {
        PULSE_PANIC("Out of timers capacity");
    }
}

} // anonymous namespace

void
TimerEntry::Fire()
{
    if (etl::holds_alternative<Task>(data)) {
        etl::get<Task>(data).Schedule();
    } else if (etl::holds_alternative<Timer::Handle>(data)) {
        etl::get<Timer::Handle>(data)->Fire();
    } else {
        PULSE_PANIC("Empty timer entry");
    }
    data.emplace<0>();
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

Timer::Timer(Timer &&other) noexcept:
    heapIdx(other.heapIdx),
    isScheduled(other.isScheduled),
    isFired(other.isFired),
    isCanceled(other.isCanceled),
    isPrevFired(other.isPrevFired)
{
    if (other.isScheduled) {
        timers.Item(heapIdx).ReplaceTimer(this);
    }
    other.isScheduled = 0;
    other.isFired = 0;
    other.isCanceled = 1;
    other.isPrevFired = 0;
    // Cancel any waiters on source timer
    other.ScheduleTasks();
}

Timer::~Timer()
{
    PULSE_ASSERT(refCounter == 0);
    Cancel();
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
Timer::Cancel()
{
    if (isScheduled) {
        timers.Remove(heapIdx);
        isScheduled = 0;
    }
    isCanceled = 1;
    return ScheduleTasks();
}

void
Timer::Schedule(TickCount time)
{
    PULSE_ASSERT(!isScheduled);
    isScheduled = 1;
    isPrevFired = isFired;
    isFired = 0;
    isCanceled = 0;
    ScheduleTimer(this, time);
}

void
Timer::Fire()
{
    isScheduled = 0;
    isFired = 1;
    ScheduleTasks();
}

int
Timer::ScheduleTasks()
{
    int n = 0;
    while (true) {
        Task task = waiters.PopFirst();
        if (!task) {
            break;
        }
        n++;
        task.Schedule();
    }
    return n;
}

bool
details::CheckTimers()
{
    bool fired = false;
    while (!timers.IsEmpty()) {
        TimerEntry &e = timers.Top();
        if (Timer::IsAfter(e.time, curTime)) {
            break;
        }
        fired = true;
        e.Fire();
        timers.PopTop();
    }
    return fired;
}

bool
DelayAwaiter::await_ready() const
{
    return !Timer::IsAfter(time, curTime);
}

void
DelayAwaiter::await_suspend(Task::CoroutineHandle handle)
{
    ScheduleTask(Task(handle), time);
}

bool
TimerAwaiter::await_ready() const
{
    return timer->IsReady();
}

bool
TimerAwaiter::await_suspend(Task::CoroutineHandle handle)
{
    Timer &t = *timer;
    // Timers fired from scheduler, not from ISR when just time is incremented, so it is safe to
    // check without critical section.
    if (t.IsReady()) {
        return false;
    }
    t.waiters.AddFirst(handle);
    return true;
}


bool
TimerAwaiter::await_resume() const
{
    if (timer->isScheduled) {
        return timer->isPrevFired;
    }
    return timer->isFired;
}

#endif // pulseConfig_ENABLE_TIMER
