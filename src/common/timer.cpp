#include <pulse/details/default_config.h>

#ifdef pulseConfig_ENABLE_TIMER

#include <pulse/timer.h>
#include <pulse/heap.h>
#include <etl/variant.h>
#include <etl/atomic.h>


using namespace pulse;
using TickCount = Timer::TickCount;


static_assert(pulseConfig_TICK_FREQ > 0,
              "pulseConfig_TICK_FREQ must be positive");

static_assert(pulseConfig_MAX_TIMERS > 0,
              "pulseConfig_MAX_TIMERS must be positive");

namespace {

etl::atomic<TickCount> curTime;

struct TimerEntry {
    //XXX do not use etl::variant due to big overhead
    etl::variant<etl::monostate, Task, Timer::Handle> data;
    TickCount time;

    void
    Fire();
};

void
TimerEntry::Fire()
{
    if (etl::holds_alternative<Task>(data)) {
        etl::get<Task>(data).Schedule();
    } else {
        //XXX
    }
    data.emplace<0>();
}

bool
IsEntryBefore(const TimerEntry &a, const TimerEntry &b)
{
    return Timer::IsBefore(a.time, b.time);
}

Heap<TimerEntry, IsEntryBefore, pulseConfig_MAX_TIMERS> timers;

void
ScheduleTask(Task task, TickCount time)
{
    timers.Insert(TimerEntry{etl::move(task), time});
}

} // anonymous namespace

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
    //XXX check timers
    return ret;
}

void
Timer::Tick()
{
    curTime++;
    // Timers fired from scheduler
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

#endif // pulseConfig_ENABLE_TIMER
