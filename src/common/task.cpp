#include <pulse/task.h>
#include <pulse/port.h>
#include <pulse/timer.h>

#include <etl/limits.h>
#include <etl/utility.h>
#include <etl/bitset.h>


using namespace pulse;


namespace {

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
TaskTailedList readyTasks[pulseConfig_NUM_TASK_PRIORITIES];

/// Each set bit corresponds to non-empty queue for corresponding priority.
PriorityBitmap readyTasksBitmap;

/// Currently running task (top-level, resumed by scheduler)
Task currentTask;

void
ScheduleTask(Task task)
{
    TaskPromise &promise = task.GetPromise();
    PULSE_ASSERT(!promise.isRunnable);
    PULSE_ASSERT(promise.priority < pulseConfig_NUM_TASK_PRIORITIES);
    TaskTailedList &list = readyTasks[promise.priority];
    CriticalSection cs;
    list.AddLast(etl::move(task));
    readyTasksBitmap.Set(promise.priority);
    promise.isRunnable = 1;
}

void
DescheduleTask(const Task &task)
{
    TaskPromise &promise = task.GetPromise();
    PULSE_ASSERT(promise.isRunnable);
    PULSE_ASSERT(promise.priority < pulseConfig_NUM_TASK_PRIORITIES);
    TaskTailedList &list = readyTasks[promise.priority];
    CriticalSection cs;
    if (!list.Remove(task)) {
        PULSE_PANIC("Task not scheduled");
    }
    if (list.IsEmpty()) {
        readyTasksBitmap.Clear(promise.priority);
    }
    promise.isRunnable = 0;
}

} // anonymous namespace

bool
Task::Unpin()
{
    if (GetPromise().Unpin()) {
        // Change state first to make iti visible to coroutine frame destructors
        auto h = handle;
        handle = CoroutineHandle();
        h.destroy();
        return true;
    }
    return false;
}

void
Task::SpawnImpl(Task task, Priority priority)
{
    TaskPromise &promise = task.GetPromise();
    promise.priority = priority;
    ScheduleTask(etl::move(task));
}

void
Task::Schedule() const &
{
    ScheduleTask(*this);
}

void
Task::Schedule() &&
{
    ScheduleTask(etl::move(*this));
}

void
Task::SetPriority(Priority priority)
{
    TaskPromise &promise = GetPromise();
    if (promise.priority == priority) {
        return;
    }
    bool wasScheduled = promise.isRunnable;
    if (wasScheduled) {
        DescheduleTask(*this);
    }
    promise.priority = priority;
    if (wasScheduled) {
        ScheduleTask(*this);
    }
}

void
Task::RaisePriority(Priority priority)
{
    TaskPromise &promise = GetPromise();
    if (promise.priority <= priority) {
        return;
    }
    bool wasScheduled = promise.isRunnable;
    if (wasScheduled) {
        DescheduleTask(*this);
    }
    promise.priority = priority;
    if (wasScheduled) {
        ScheduleTask(*this);
    }
}

void
Task::RunScheduler()
{
    while (true) {
        RunSome();
        pulsePort_EnableInterrupts();
        pulsePort_Sleep();
    }
}

void
Task::RunSome()
{
    while (true) {
        details::CheckTimers();
        CriticalSection cs;
        if (!readyTasksBitmap) {
            break;
        }
        uint8_t pri = readyTasksBitmap.FirstSet();
        PULSE_ASSERT(pri < pulseConfig_NUM_TASK_PRIORITIES);
        TaskTailedList &list = readyTasks[pri];
        currentTask = list.PopFirst();
        PULSE_ASSERT(currentTask);
        if (list.IsEmpty()) {
            readyTasksBitmap.Clear(pri);
        }
        PULSE_ASSERT(currentTask.GetPromise().isRunnable);
        currentTask.GetPromise().isRunnable = 0;
        cs.Exit();
        currentTask.Resume();
        currentTask.ReleaseHandle();
    }
}

Task
Task::GetCurrent()
{
    return currentTask;
}

bool
TaskSwitchAwaiter::await_suspend(Task::CoroutineHandle handle)
{
    Task task(handle);
    TaskPromise &promise = task.GetPromise();
    Task::Priority pri = promise.priority;
    CriticalSection cs;
    if (!readyTasksBitmap || readyTasksBitmap.FirstSet() > pri) {
        // Do not suspend calling coroutine if there is no other runnable tasks or they have lower
        // priority.
        return false;
    }
    TaskTailedList &list = readyTasks[pri];
    list.AddLast(etl::move(task));
    readyTasksBitmap.Set(pri);
    promise.isRunnable = 1;
    switched = true;
    return true;
}

bool
Task::AwaitResult(details::TaskAwaiterBase *waiter, const Task &task) const
{
    PULSE_ASSERT(task.handle.framePtr != handle.framePtr);
    Task::Priority priority = task.GetPromise().priority;
    TaskPromise &promise = GetPromise();
    if (promise.isFinished) {
        return false;
    }
    promise.resultWaiters.AddFirst(waiter);
    if (priority < promise.priority) {
        // Propagate waiting task higher priority to this task.
        bool reschedule = promise.isRunnable;
        if (reschedule) {
            // Remove it from current queue first.
            DescheduleTask(*this);
        }
        promise.priority = priority;
        if (reschedule) {
            ScheduleTask(*this);
        }
    }
    return true;
}

void
Task::CancelAwaitResult(details::TaskAwaiterBase *waiter) const
{
    GetPromise().resultWaiters.Remove(waiter);
}


TaskPromise::TaskPromise()
{
    Task currentTask = Task::GetCurrent();
    if (currentTask) {
        priority = currentTask.GetPromise().priority;
    }
}

TaskPromise::~TaskPromise()
{
    PULSE_ASSERT(refCounter == 0);
    if (weakPtrTag) {
        weakPtrTag->handle.reset();
    }
}

void
TaskPromise::AddRef()
{
    auto cur = refCounter.load();
    while (true) {
        // refCounter should never reach zero when using this method since it is always should be
        // used on some task which holds a reference.
        PULSE_ASSERT(cur != 0);
        if (cur == etl::numeric_limits<decltype(cur)>::max() ||
            cur == etl::numeric_limits<decltype(cur)>::min()) {

            PULSE_PANIC("Task reference counter overflow");
        }
        auto newValue = cur >= 0 ? cur + 1 : cur - 1;
        if (refCounter.compare_exchange_weak(cur, newValue)) {
            break;
        }
    }
}

bool
TaskPromise::TryAddRef()
{
    auto cur = refCounter.load();
    while (cur != 0) {
        if (cur == etl::numeric_limits<decltype(cur)>::max() ||
            cur == etl::numeric_limits<decltype(cur)>::min()) {

            PULSE_PANIC("Task reference counter overflow");
        }
        auto newValue = cur >= 0 ? cur + 1 : cur - 1;
        if (refCounter.compare_exchange_weak(cur, newValue)) {
            return true;
        }
    }
    // Reference counter reached zero but task is not yet destructed.
    return false;
}

bool
TaskPromise::ReleaseRef()
{
    auto cur = refCounter.load();
    while (true) {
        if (cur == 0 || cur == -1) {
            PULSE_PANIC("Task reference counter underflow");
        }
        auto newValue = cur > 0 ? cur - 1 : cur + 1;
        if (refCounter.compare_exchange_weak(cur, newValue)) {
            return newValue == 0;
        }
    }
}

void
TaskPromise::Pin()
{
    auto cur = refCounter.load();
    while (true) {
        if (cur == 0) {
            PULSE_PANIC("Pinning unreferenced task");
        }
        if (cur < 0) {
            // Already pinned
            break;
        }
        if (refCounter.compare_exchange_weak(cur, -cur - 1)) {
            break;
        }
    }
}

bool
TaskPromise::Unpin()
{
    auto cur = refCounter.load();
    while (true) {
        if (cur == 0) {
            PULSE_PANIC("Unpinning unreferenced task");
        }
        if (cur > 0) {
            // Not pinned
            return false;
        }
        auto newValue = -cur - 1;
        if (refCounter.compare_exchange_weak(cur, newValue)) {
            return newValue == 0;
        }
    }
}

Task::WeakPtr
TaskPromise::GetWeakPtr()
{
    if (weakPtrTag) {
        return weakPtrTag;
    }
    auto tag = new details::TaskWeakPtrTag(Task::CoroutineHandle::from_promise(*this));
    if (!tag) {
        PULSE_PANIC("TaskWeakPtrTag allocation failed");
    }
    CriticalSection cs;
    if (weakPtrTag) {
        cs.Exit();
        delete tag;
    } else {
        weakPtrTag = SharedPtr<details::TaskWeakPtrTag>(tag, true);
    }
    return weakPtrTag;
}

void
TaskPromise::NotifyWaiters()
{
    while (true) {
        auto waiter = resultWaiters.PopFirst();
        if (!waiter) {
            break;
        }
        waiter->waiter.Wakeup();
    }
}


Task
details::TaskWeakPtr::Lock()
{
    if (!tag) {
        return nullptr;
    }
    return Task(tag->handle, Task::WeakTag());
}


void
details::MultipleTasksAwaiterBase::Finish(Entry *tasks, size_t numTasks)
{
    for (size_t i = 0; i < numTasks; i++) {
        Entry &e = tasks[i];
        if (!e.target) {
            continue;
        }
        CriticalSection cs;
        TaskPromise &handlerPromise = e.handler.GetPromise();
        if (!handlerPromise.isFinished && handlerPromise.isRunnable) {
            DescheduleTask(e.handler);
        }
        TaskPromise &targetPromise = e.target.GetPromise();
        if (!targetPromise.isFinished && targetPromise.isRunnable) {
            DescheduleTask(e.target);
        }
    }
}
