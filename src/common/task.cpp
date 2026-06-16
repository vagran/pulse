#include <pulse/task.h>
#include <pulse/port.h>
#include <pulse/timer.h>
#include <pulse/pool.h>
#include <pulse/details/pulse_log.h>

#include <etl/limits.h>
#include <etl/utility.h>
#include <etl/bitset.h>


using namespace pulse;


namespace {

#if pulseConfig_SCHEDULER_STATS

namespace stats {

etl::atomic<uint32_t> numActiveTasks{0},
                      numFreeTasks{pulseConfig_NUM_PREALLOCED_TASKS},
                      numDynamicAllocations{0};

} // namespace stats

#define INC_STAT(name) stats::name.fetch_add(1)
#define DEC_STAT(name) stats::name.fetch_sub(1)

#else // pulseConfig_SCHEDULER_STATS

#define INC_STAT(name)
#define DEC_STAT(name)

#endif // pulseConfig_SCHEDULER_STATS

class PriorityBitmap {
public:
    using Bitmap = SizedUint<pulseConfig_NUM_TASK_PRIORITIES>;

    Bitmap bitmap = 0;

    operator bool() const
    {
        return bitmap != 0;
    }

    void
    Set(uint8_t priority)
    {
        bitmap |= Mask(priority);
    }

    void
    Clear(uint8_t priority)
    {
        bitmap &= ~Mask(priority);
    }

    /** @return Index of highest priority bit set. May be greater or equal to
     * pulseConfig_NUM_TASK_PRIORITIES if no bits set.
     */
    uint8_t
    FirstSet() const
    {
        return etl::countr_zero(bitmap);
    }

private:
    static constexpr Bitmap
    Mask(uint8_t priority)
    {
        return static_cast<Bitmap>(1) << priority;
    }
};

static_assert(pulseConfig_NUM_TASK_PRIORITIES >= 2,
              "pulseConfig_NUM_TASK_PRIORITIES must be at least 2");

static_assert(pulseConfig_NUM_TASK_PRIORITIES < sizeof(PriorityBitmap) * 8,
              "pulseConfig_NUM_TASK_PRIORITIES too big");

/// Tasks in runnable state, arranged by priority.
details::TaskTailedList readyTasks[pulseConfig_NUM_TASK_PRIORITIES];

/// Each set bit corresponds to non-empty queue for corresponding priority.
PriorityBitmap readyTasksBitmap;

/// Currently running task (top-level, resumed by scheduler)
TaskRef currentTask;

struct CbPoolTrait {
    static void
    OnAllocated()
    {
        INC_STAT(numActiveTasks);
    }

    static void
    OnPoolAllocated()
    {
        DEC_STAT(numFreeTasks);
    }

    static void
    OnDynamicallyAllocated()
    {
        INC_STAT(numDynamicAllocations);
        PULSE_LOG_INFO("Allocating task CB from heap");
    }

    static void
    OnFreed()
    {
        DEC_STAT(numActiveTasks);
        INC_STAT(numFreeTasks);
    }
};

Pool<details::TaskCb, pulseConfig_NUM_PREALLOCED_TASKS, true, CriticalSection, CbPoolTrait> cbPool;

using SleepInterruptsGuard = pulsePort_SleepInterruptGuard;

} // anonymous namespace


class details::TaskImpl {
public:
    static void
    ScheduleTask(TaskCb &cb);

    /// Deschedule task if scheduled.
    // @return True if descheduled, false if was not scheduled.
    static bool
    TryDescheduleTask(TaskCb &cb);

    static void
    Spawn(const TaskRef &task, tasks::Priority priority);

    /**
     * @param nextTimerTicks If specified, the method prepares for sleeping until next interrupt
     *  (interrupts are disabled on exit). Number of ticks until next scheduled timer is stored in
     *  this variable, MAX_TICK_COUNT if no next timer.
     * @return Guard for disabled interrupts if any.
     */
    static SleepInterruptsGuard
    RunSomeImpl(Timer::TickCount *nextTimerTicks);
};

void
details::TaskImpl::ScheduleTask(TaskCb &cb)
{
    PULSE_ASSERT(!cb.isFinished);
    PULSE_ASSERT(cb.priority < pulseConfig_NUM_TASK_PRIORITIES);

    TaskTailedList &list = readyTasks[cb.priority];
    cb.AddRef();

    CriticalSection cs;
    PULSE_ASSERT(!cb.isRunnable);
    list.AddLast(&cb);
    readyTasksBitmap.Set(cb.priority);
    cb.isRunnable = 1;
}

bool
details::TaskImpl::TryDescheduleTask(TaskCb &cb)
{
    PULSE_ASSERT(cb.priority < pulseConfig_NUM_TASK_PRIORITIES);
    TaskTailedList &list = readyTasks[cb.priority];
    CriticalSection cs;
    if (!cb.isRunnable) {
        return false;
    }
    if (!list.Remove(&cb)) {
        PULSE_PANIC("Task not scheduled");
    }
    if (list.IsEmpty()) {
        readyTasksBitmap.Clear(cb.priority);
    }
    cb.isRunnable = 0;
    cs.Exit();
    cb.ReleaseRef();
    return true;
}

void
details::TaskImpl::Spawn(const TaskRef &task, tasks::Priority priority)
{
    PULSE_ASSERT(!task.cb->isRunnable);
    task.cb->priority = priority;
    ScheduleTask(*task.cb);
}

SleepInterruptsGuard
details::TaskImpl::RunSomeImpl(Timer::TickCount *nextTimerTicks)
{
    Timer::TickCount ticks = details::CheckTimers();
    bool timersChecked = true,
         shouldRecheckTimers = false;

    while (true) {
        if (shouldRecheckTimers) {
            ticks = details::CheckTimers();
            timersChecked = true;
            shouldRecheckTimers = false;
        }
        SleepInterruptsGuard ig;
        if (!readyTasksBitmap) {
            if (!timersChecked) {
                // Some tasks were run since last timer check, new timers might be scheduled so need
                // to re-check them.
                shouldRecheckTimers = true;
                continue;
            }
            if (nextTimerTicks) {
                *nextTimerTicks = ticks;
                return ig;
            }
            break;
        }
        timersChecked = false;
        uint8_t pri = readyTasksBitmap.FirstSet();
        PULSE_ASSERT(pri < pulseConfig_NUM_TASK_PRIORITIES);
        TaskTailedList &list = readyTasks[pri];
        // Reference transferred from list.
        TaskWeakRef weakRef(details::TaskCbPtr(list.PopFirst(), true));
        TaskCb *cb = weakRef.cb.Get();
        PULSE_ASSERT(cb);
        if (list.IsEmpty()) {
            readyTasksBitmap.Clear(pri);
        }
        // Even destroyed task should still be runnable if queued
        PULSE_ASSERT(cb->isRunnable);
        cb->isRunnable = 0;
        ig.Exit();
        currentTask = weakRef.Lock();
        if (currentTask) {
            cb->Resume();
            currentTask.ReleaseHandle();
        }
    }
    return SleepInterruptsGuard(false);
}

void
details::TaskSpawnImpl(const TaskRef &task, tasks::Priority priority)
{
    TaskImpl::Spawn(task, priority);
}


TaskRef::TaskRef(tasks::CoroutineHandle coro):
    TaskRef(coro ? coro.promise().cb : nullptr)
{
    if (coro) {
        cb->CoroAddRef();
    }
}

void
TaskRef::ReleaseHandle()
{
    if (!cb) {
        return;
    }
    if (cb->CoroReleaseRef()) {
        PULSE_ASSERT(cb->coro);
        cb->coro.destroy();
        cb->coro = tasks::CoroutineHandle();
    }
    cb.Reset();
}

bool
TaskRef::Unpin() const
{
    PULSE_ASSERT(cb);
    PULSE_ASSERT(cb->coro);
    if (cb->Unpin()) {
        cb->coro.destroy();
        cb->coro = tasks::CoroutineHandle();
        return true;
    }
    return false;
}

void
TaskRef::Schedule() const
{
    PULSE_ASSERT(cb);
    details::TaskImpl::ScheduleTask(*cb);
}

TaskRef
TaskWeakRef::Lock()
{
    if (!cb || !cb->CoroTryAddRef()) {
        return TaskRef();
    }
    return TaskRef(cb);
}

details::TaskCb *
details::TaskCb::Allocate()
{
    return cbPool.Allocate();
}

void
details::TaskCb::Free()
{
    PULSE_ASSERT(refCounter == 0);
    PULSE_ASSERT(coroRefCounter == 0);
    cbPool.Free(this);
}

void
details::TaskCb::CoroAddRef()
{
    auto cur = coroRefCounter.load();
    while (true) {
        // refCounter should never reach zero when using this method since it is always should be
        // used on some task which holds a reference.
        PULSE_ASSERT(cur != 0);
        if (cur == etl::numeric_limits<decltype(cur)>::max() ||
            cur == etl::numeric_limits<decltype(cur)>::min()) {

            PULSE_PANIC("Task reference counter overflow");
        }
        decltype(cur) newValue = cur >= 0 ? cur + 1 : cur - 1;
        if (coroRefCounter.compare_exchange_weak(cur, newValue)) {
            break;
        }
    }
}

bool
details::TaskCb::CoroTryAddRef()
{
    auto cur = coroRefCounter.load();
    while (cur != 0) {
        if (cur == etl::numeric_limits<decltype(cur)>::max() ||
            cur == etl::numeric_limits<decltype(cur)>::min()) {

            PULSE_PANIC("Task reference counter overflow");
        }
        decltype(cur) newValue = cur >= 0 ? cur + 1 : cur - 1;
        if (coroRefCounter.compare_exchange_weak(cur, newValue)) {
            return true;
        }
    }
    // Reference counter reached zero but task is not yet destructed.
    return false;
}

bool
details::TaskCb::CoroReleaseRef()
{
    auto cur = coroRefCounter.load();
    while (true) {
        if (cur == 0 || cur == -1) {
            PULSE_PANIC("Task reference counter underflow");
        }
        decltype(cur) newValue = cur > 0 ? cur - 1 : cur + 1;
        if (coroRefCounter.compare_exchange_weak(cur, newValue)) {
            return newValue == 0;
        }
    }
}

void
details::TaskCb::Pin()
{
    auto cur = coroRefCounter.load();
    while (true) {
        if (cur == 0) {
            PULSE_PANIC("Pinning unreferenced task");
        }
        if (cur < 0) {
            // Already pinned
            break;
        }
        if (coroRefCounter.compare_exchange_weak(cur, -cur - 1)) {
            break;
        }
    }
}

bool
details::TaskCb::Unpin()
{
    auto cur = coroRefCounter.load();
    while (true) {
        if (cur == 0) {
            PULSE_PANIC("Unpinning unreferenced task");
        }
        if (cur > 0) {
            // Not pinned
            return false;
        }
        decltype(cur) newValue = -cur - 1;
        if (coroRefCounter.compare_exchange_weak(cur, newValue)) {
            return newValue == 0;
        }
    }
}

bool
details::TaskCb::AwaitResult(details::TaskAwaiterBase *waiter, const TaskRef &waiterTask)
{
    if (waiterTask.cb->coro == coro) {
        PULSE_PANIC("Task awaits itself");
    }
    if (isFinished) {
        return false;
    }
    resultWaiters.AddFirst(waiter);
    tasks::Priority priority = waiterTask.cb->priority;
    if (priority < this->priority) {
        // Propagate waiting task higher priority to this task.
        RaisePriority(priority);
    }
    return true;
}

void
details::TaskCb::CancelAwaitResult(details::TaskAwaiterBase *waiter)
{
    bool res PULSE_UNUSED = resultWaiters.Remove(waiter);
    PULSE_ASSERT(res);
}

void
details::TaskCb::NotifyWaiters()
{
    while (true) {
        auto waiter = resultWaiters.PopFirst();
        if (!waiter) {
            break;
        }
        waiter->waiter.Wakeup();
    }
}

void
details::TaskCb::Resume()
{
    PULSE_ASSERT(coro);
    coro.resume();
}

void
details::TaskCb::SetPriority(tasks::Priority priority)
{
    if (this->priority == priority) {
        return;
    }
    bool wasScheduled = details::TaskImpl::TryDescheduleTask(*this);
    this->priority = priority;
    if (wasScheduled) {
        details::TaskImpl::ScheduleTask(*this);
    }
}

void
details::TaskCb::RaisePriority(tasks::Priority priority)
{
    if (this->priority <= priority) {
        return;
    }
    bool wasScheduled = details::TaskImpl::TryDescheduleTask(*this);
    this->priority = priority;
    if (wasScheduled) {
        details::TaskImpl::ScheduleTask(*this);
    }
}


details::TaskPromiseBase::TaskPromiseBase():
    cb(TaskCbPtr(TaskCb::Allocate(), true))
{
    cb->coro = tasks::CoroutineHandle::from_promise(*this);
    if (currentTask) {
        cb->priority = currentTask.cb->priority;
    }
}

details::TaskPromiseBase::~TaskPromiseBase()
{
    PULSE_ASSERT(cb->coroRefCounter == 0);
}

bool
TaskSwitchAwaiter::await_suspend(tasks::CoroutineHandle handle)
{
    TaskRef task(handle);
    details::TaskCb &cb = *task.cb;
    tasks::Priority pri = cb.priority;
    CriticalSection cs;
    if (!readyTasksBitmap || readyTasksBitmap.FirstSet() > pri) {
        // Do not suspend calling coroutine if there is no other runnable tasks or they have lower
        // priority.
        return false;
    }
    details::TaskTailedList &list = readyTasks[pri];
    cb.AddRef();
    list.AddLast(&cb);
    readyTasksBitmap.Set(pri);
    cb.isRunnable = 1;
    switched = true;
    return true;
}

TaskRef
tasks::GetCurrentTask()
{
    return currentTask;
}

void
tasks::RunScheduler()
{
    pulsePort_InitScheduler();
    while (true) {
        Timer::TickCount nextTimerTicks;
        SleepInterruptsGuard ig = details::TaskImpl::RunSomeImpl(&nextTimerTicks);
#if pulseConfig_TICKLESS_IDLE
        if (nextTimerTicks < pulseConfig_TICKLESS_MIN_TICKS) {
            pulsePort_Sleep();
        } else {
            Timer::TickCount passed = pulsePort_TicklessSleep(nextTimerTicks);
            Timer::SetTime(Timer::GetTime() + passed);
        }
#else // pulseConfig_TICKLESS_IDLE
        pulsePort_Sleep();
#endif // pulseConfig_TICKLESS_IDLE
    }
}

void
tasks::RunSome()
{
    details::TaskImpl::RunSomeImpl(nullptr);
}

#if pulseConfig_SCHEDULER_STATS

void
tasks::GetSchedulerStats(SchedulerStats &stats)
{
    using namespace stats;
    stats.numActiveTasks = numActiveTasks;
    stats.numFreeTasks = numFreeTasks;
    stats.numDynamicAllocations = numDynamicAllocations;
}

#endif // pulseConfig_SCHEDULER_STATS
