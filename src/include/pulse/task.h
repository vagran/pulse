#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <pulse/config.h>
#include <pulse/details/common.h>
#include <pulse/coroutine.h>
#include <pulse/list.h>
#include <etl/bit.h>
#include <etl/memory.h>
#include <etl/span.h>


namespace pulse {

class TaskPromise;

template <typename TRet, bool initialSuspend>
class TTaskPromise;

template <typename TRet, bool initialSuspend = true>
class TTask;

template <typename TRet>
class TaskAwaiter;

template <size_t NumTasks = etl::dynamic_extent>
class AllTasksAwaiter;

template <size_t NumTasks = etl::dynamic_extent>
class AnyTaskAwaiter;


/// Smart pointer for coroutine frame.
class Task {
public:
    using TPromise = TaskPromise;

    static constexpr int NUM_PRIO_BITS = BitWidth(pulseConfig_NUM_TASK_PRIORITIES - 1);

    using Priority = uint8_t;

    static constexpr Priority HIGHEST_PRIORITY = 0,
                              ISR_PRIORITY = HIGHEST_PRIORITY,
                              LOWEST_PRIORITY = pulseConfig_NUM_TASK_PRIORITIES - 1;

    using CoroutineHandle = std::coroutine_handle<TaskPromise>;

    // Awaiter for explicit task switching.
    class TaskSwitchAwaiter {
    public:
        bool
        await_ready() const
        {
            return false;
        }

        bool
        await_suspend(Task::CoroutineHandle handle);

        /// @return True if task was switched, false if immediately returned to calling task.
        bool
        await_resume() const
        {
            return switched;
        }

    private:
        bool switched = false;
    };


    Task() = default;

    Task(etl::nullptr_t):
        Task()
    {}

    inline
    Task(CoroutineHandle handle);

    inline
    Task(const Task &other);

    Task(Task &&other) noexcept:
        handle(other.handle)
    {
        other.handle = CoroutineHandle();
    }

    ~Task()
    {
        ReleaseHandle();
    }

    inline Task &
    operator =(const Task &other);

    inline Task &
    operator =(Task &&other) noexcept;

    bool
    operator ==(const Task &other) const
    {
        return handle == other.handle;
    }

    bool
    operator !=(const Task &other) const
    {
        return handle != other.handle;
    }

    /// Clear associated handle, making this task null.
    inline void
    ReleaseHandle();

    TPromise &
    GetPromise() const
    {
        PULSE_ASSERT(handle);
        return handle.promise();
    }

    /// @return True if bound to a valid coroutine frame.
    operator bool() const
    {
        return handle;
    }

    /** Enqueue this task into ready tasks queue according to its current priority. Can be called
     * from ISR.
     */
    void
    Schedule() const &;

    void
    Schedule() &&;

    inline bool
    IsFinished() const;

    void
    SetPriority(Priority priority);

    template <typename TRet>
    static TTask<TRet, true>
    Spawn(TTask<TRet, true> task, Priority priority = LOWEST_PRIORITY)
    {
        SpawnImpl(task, priority);
        return etl::move(task);
    }

    /**
     * Run main loop of tasks scheduler. Never returns.
     */
    static void
    RunScheduler();

    /** Run tasks which are currently in runnable state. Exits when no more runnable task. */
    static void
    RunSome();

    /** Switch to other runnable task if any. `co_await` returns true if task was switched, false if
     * immediately returned to calling task.
     * @code
     * co_await Task::Switch();
     * @endcode
     */
    static TaskSwitchAwaiter
    Switch()
    {
        return {};
    }

    inline TaskAwaiter<void>
    Wait() const;

    inline TaskAwaiter<void>
    operator co_await() const;

    /** Wait when all of the provided tasks complete. Uses dynamic allocation for tasks list if
     * `NumTasks` is `etl::dynamic_extent`.
     */
    template<size_t NumTasks = etl::dynamic_extent>
    static inline AllTasksAwaiter<NumTasks>
    WhenAll(const etl::span<Task, NumTasks> &tasks);

    /* Wait when any of the provided tasks completes. `co_await` returns index of the first
     * completed task. Uses dynamic allocation for tasks list if `NumTasks` is
     * `etl::dynamic_extent`.
     */
    template<size_t NumTasks = etl::dynamic_extent>
    static inline AnyTaskAwaiter<NumTasks>
    WhenAny(const etl::span<Task, NumTasks> &tasks);

    template <etl::derived_from<Task> T, etl::derived_from<Task>... Args>
    static AllTasksAwaiter<sizeof...(Args) + 1>
    WhenAll(T task, Args... tasks);

    template <etl::derived_from<Task> T, etl::derived_from<Task>... Args>
    static AnyTaskAwaiter<sizeof...(Args) + 1>
    WhenAny(T task, Args... tasks);

protected:
    friend class TaskPromise;

    template <typename, bool>
    friend class TTaskPromise;

    template<typename>
    friend class TaskAwaiter;

    CoroutineHandle handle;

    void
    Resume()
    {
        if (!handle.done()) {
            handle.resume();
        }
    }

private:
    /** Spawns new task with the specified priority. Returns ID_NONE if failed (if
     * `pulseConfig_PANIC_ON_TASK_SPAWN_FAILURE` disabled, panics otherwise).
     * @param task Task object returned by coroutine function.
     * @param priority Task priority.
     */
    static void
    SpawnImpl(Task task, Priority priority = LOWEST_PRIORITY);

    bool
    AwaitResult(Task task) const;
};


template <typename TRet, bool initialSuspend>
class TTask: public Task {
public:
    using TPromise = TTaskPromise<TRet, initialSuspend>;

    using Task::Task;

    TPromise &
    GetPromise() const
    {
        PULSE_ASSERT(handle);
        return reinterpret_cast<TPromise &>(handle.promise());
    }

    inline const TRet &
    GetResult() const;

    inline TaskAwaiter<TRet>
    Wait() const;

    inline TaskAwaiter<TRet>
    operator co_await() const;
};

template <bool initialSuspend>
class TTask<void, initialSuspend>: public Task {
public:
    using TPromise = TTaskPromise<void, initialSuspend>;

    using Task::Task;

    TPromise &
    GetPromise() const
    {
        PULSE_ASSERT(handle);
        return reinterpret_cast<TPromise &>(handle.promise());
    }

    inline TaskAwaiter<void>
    Wait() const;

    inline TaskAwaiter<void>
    operator co_await() const;
};

using TaskV = TTask<void>;

/// Return this type from non-top-level async functions.
template <typename TRet>
using Awaitable = TTask<TRet, false>;

namespace details {

inline TaskPromise &
GetTaskPromise(const Task &task)
{
    return task.GetPromise();
}

} // namespace details


/** Also acts as task control block. */
class TaskPromise {
public:
    /// Next task when in list, none if last one.
    Task next = nullptr;
    /// Tasks currently awaiting this task finishing.
    details::ListWeak<Task, details::GetTaskPromise> resultWaiters;
    uint8_t refCounter = 0;
    uint8_t priority: Task::NUM_PRIO_BITS = Task::LOWEST_PRIORITY,
    /// Task currently queued in runnable queue.
            isRunnable: 1 = 0,
    /// Task finished and result is available.
            isFinished: 1 = 0;

    TaskPromise() = default;

    // No need to make it virtual since promise object is always constructed and destructed from
    // coroutine frame constructor/destructor by concrete type.
    ~TaskPromise()
    {
        PULSE_ASSERT(refCounter == 0);
    }

    /// Add reference from new Task instance.
    void
    AddRef()
    {
        if (refCounter == etl::numeric_limits<decltype(refCounter)>::max()) {
            PULSE_PANIC("Task reference counter overflow");
        }
        refCounter++;
    }

    /// Release reference from Task instance.
    /// @return True if last reference released.
    bool
    ReleaseRef()
    {
        PULSE_ASSERT(refCounter != 0);
        return --refCounter == 0;
    }

    std::suspend_always
    final_suspend() noexcept
    {
        // Should suspend in order to prevent coroutine frame destruction by coroutine finishing.
        // It should only be destructed by releasing last reference from last Task instance.
        return {};
    }

    void
    unhandled_exception()
    {
        PULSE_PANIC("TaskPromise::unhandled_exception");
    }

protected:

    /// Wake up all tasks waiting for this task completion.
    void
    NotifyWaiters();
};


using TaskList = List<Task, details::GetTaskPromise>;
using TaskTailedList = TailedList<Task, details::GetTaskPromise>;


/** @tparam initialSuspend Enables initial suspend when true. Tasks spawned by scheduler typically
 * should have it true so that initial body invocation is done when first switched to this task. In
 * contrast, awaitable returned from async function should have it false so that it runs till first
 * suspension point (if any).
 */
template <typename TRet, bool initialSuspend>
class TTaskPromise: public TaskPromise {
public:
    ~TTaskPromise()
    {
        if (isFinished) {
            etl::destroy_at(&GetResult());
        }
    }

    etl::conditional_t<initialSuspend, std::suspend_always, std::suspend_never>
    initial_suspend()
    {
        return {};
    }

    TTask<TRet, initialSuspend>
    get_return_object()
    {
        return Task::CoroutineHandle::from_promise(*this);
    }

    template<etl::convertible_to<TRet> From>
    void
    return_value(From&& from)
    {
        isFinished = 1;
        new (result.data) TRet(etl::forward<From>(from));
        NotifyWaiters();
    }

    const TRet &
    GetResult() const
    {
        PULSE_ASSERT(isFinished);
        return *reinterpret_cast<const TRet *>(result.data);
    }

private:
    struct alignas(TRet) {
        uint8_t data[sizeof(TRet)];
    } result;
};

template <bool initialSuspend>
class TTaskPromise<void, initialSuspend>: public TaskPromise {
public:
    etl::conditional_t<initialSuspend, std::suspend_always, std::suspend_never>
    initial_suspend()
    {
        return {};
    }

    TTask<void, initialSuspend>
    get_return_object()
    {
        return Task::CoroutineHandle::from_promise(*this);
    }

    void
    return_void()
    {
        isFinished = 1;
        NotifyWaiters();
    }
};


template <typename TRet>
class TaskAwaiter {
public:
    template <bool initialSuspend>
    TaskAwaiter(TTask<TRet, initialSuspend> task):
        task(etl::move(task)),
        initialSuspend(initialSuspend)
    {}

    bool
    await_ready() const
    {
        return task.IsFinished();
    }

    bool
    await_suspend(Task::CoroutineHandle handle)
    {
        return task.AwaitResult(Task(handle));
    }

    TRet
    await_resume() const
    {
        if (initialSuspend) {
            return TTask<TRet, true>(task.handle).GetPromise().GetResult();
        } else {
            return TTask<TRet, false>(task.handle).GetPromise().GetResult();
        }
    }

private:
    const Task task;
    const bool initialSuspend;
};


template <>
class TaskAwaiter<void> {
public:
    TaskAwaiter(Task task):
        task(etl::move(task))
    {}

    bool
    await_ready() const
    {
        return task.IsFinished();
    }

    bool
    await_suspend(Task::CoroutineHandle handle)
    {
        return task.AwaitResult(Task(handle));
    }

    void
    await_resume() const
    {}

private:
    const Task task;
};


namespace details {

class MultipleTasksAwaiterBase {
protected:
    struct Entry {
        Task target, handler;
    };

    Task waiter;

    static void
    Finish(Entry *tasks, size_t numTasks);
};


template <size_t NumTasks = etl::dynamic_extent>
class MultipleTasksAwaiter: protected MultipleTasksAwaiterBase {
protected:
    etl::array<Entry, NumTasks> tasks;
    static constexpr size_t numTasks = NumTasks;
    bool isFinished = false;

    MultipleTasksAwaiter(const etl::span<Task, NumTasks> &)
    {}

    ~MultipleTasksAwaiter()
    {
        Finish();
    }

    void
    Finish()
    {
        if (!isFinished) {
            MultipleTasksAwaiterBase::Finish(tasks.data(), numTasks);
            isFinished = true;
        }
    }
};

template <>
class MultipleTasksAwaiter<etl::dynamic_extent>: protected MultipleTasksAwaiterBase {
protected:
    etl::unique_ptr<Entry[]> tasks;
    const size_t numTasks;

    MultipleTasksAwaiter(const etl::span<Task, etl::dynamic_extent> &tasks):
        tasks(new Entry[tasks.size()]),
        numTasks(tasks.size())
    {}

    ~MultipleTasksAwaiter()
    {
        Finish();
    }

    void
    Finish()
    {
        if (tasks) {
            MultipleTasksAwaiterBase::Finish(tasks.get(), numTasks);
            tasks.reset();
        }
    }
};

} // namespace details


template <size_t NumTasks>
class AllTasksAwaiter: public details::MultipleTasksAwaiter<NumTasks> {
public:
    AllTasksAwaiter(const etl::span<Task, NumTasks> &tasks);

    bool
    await_ready() const
    {
        return numLeft == 0;
    }

    bool
    await_suspend(Task::CoroutineHandle handle);

    void
    await_resume() const
    {}

private:
    using Base = details::MultipleTasksAwaiter<NumTasks>;
    using Entry = Base::Entry;

    size_t numLeft;
};


template <size_t NumTasks>
class AnyTaskAwaiter: public details::MultipleTasksAwaiter<NumTasks>  {
public:
    AnyTaskAwaiter(const etl::span<Task, NumTasks> &tasks);

    bool
    await_ready() const
    {
        return result != NONE;
    }

    bool
    await_suspend(Task::CoroutineHandle handle);

    /// @return First completed task index.
    size_t
    await_resume() const
    {
        return result;
    }

private:
    using Base = details::MultipleTasksAwaiter<NumTasks>;
    using Entry = Base::Entry;

    static constexpr size_t NONE = etl::numeric_limits<size_t>::max();

    size_t result = NONE;
};


Task::Task(CoroutineHandle handle):
    handle(handle)
{
    GetPromise().AddRef();
}

Task::Task(const Task &other):
    handle(other.handle)
{
    if (handle) {
        GetPromise().AddRef();
    }
}

Task &
Task::operator =(const Task &other)
{
    ReleaseHandle();
    handle = other.handle;
    if (handle) {
        GetPromise().AddRef();
    }
    return *this;
}

Task &
Task::operator =(Task &&other) noexcept
{
    ReleaseHandle();
    handle = other.handle;
    other.handle = CoroutineHandle();
    return *this;
}

bool
Task::IsFinished() const
{
    PULSE_ASSERT(handle);
    return GetPromise().isFinished;
}

TaskAwaiter<void>
Task::Wait() const
{
    return TaskAwaiter<void>(handle);
}

TaskAwaiter<void>
Task::operator co_await() const
{
    return Wait();
}

template<size_t NumTasks>
AllTasksAwaiter<NumTasks>
Task::WhenAll(const etl::span<Task, NumTasks> &tasks)
{
    return AllTasksAwaiter(tasks);
}

template<size_t NumTasks>
AnyTaskAwaiter<NumTasks>
Task::WhenAny(const etl::span<Task, NumTasks> &tasks)
{
    return AnyTaskAwaiter(tasks);
}

template <etl::derived_from<Task> T, etl::derived_from<Task>... Args>
AllTasksAwaiter<sizeof...(Args) + 1>
Task::WhenAll(T task, Args... tasks)
{
    Task _tasks[] = {task, tasks...};
    return WhenAll(etl::span<Task, sizeof...(Args) + 1>(_tasks));
}

template <etl::derived_from<Task> T, etl::derived_from<Task>... Args>
AnyTaskAwaiter<sizeof...(Args) + 1>
Task::WhenAny(T task, Args... tasks)
{
    Task _tasks[] = {task, tasks...};
    return WhenAny(etl::span<Task, sizeof...(Args) + 1>(_tasks));
}

void
Task::ReleaseHandle()
{
    if (handle) {
        if (GetPromise().ReleaseRef()) {
            handle.destroy();
        }
        handle = CoroutineHandle();
    }
}


template <typename TRet, bool initialSuspend>
const TRet &
TTask<TRet, initialSuspend>::GetResult() const
{
    return GetPromise().GetResult();
}

template <typename TRet, bool initialSuspend>
TaskAwaiter<TRet>
TTask<TRet, initialSuspend>::Wait() const
{
    return TaskAwaiter<TRet>(*this);
}

template <typename TRet, bool initialSuspend>
TaskAwaiter<TRet>
TTask<TRet, initialSuspend>::operator co_await() const
{
    return Wait();
}

template <bool initialSuspend>
TaskAwaiter<void>
TTask<void, initialSuspend>::Wait() const
{
    return TaskAwaiter<void>(handle);
}

template <bool initialSuspend>
TaskAwaiter<void>
TTask<void, initialSuspend>::operator co_await() const
{
    return Wait();
}


/// For returning from async functions not meant to be spawned as scheduler tasks.
template <typename TRet>
using Awaitable = TTask<TRet, false>;


template <size_t NumTasks>
AllTasksAwaiter<NumTasks>::AllTasksAwaiter(const etl::span<Task, NumTasks> &tasks):
    details::MultipleTasksAwaiter<NumTasks>(tasks),
    numLeft(Base::numTasks)
{
    auto handler = [this](size_t index) -> Awaitable<void> {
        Entry &e = this->tasks[index];

        co_await e.target;

        e.target.ReleaseHandle();
        e.handler.ReleaseHandle();

        numLeft--;
        if (numLeft == 0 && Base::waiter) {
            Base::waiter.Schedule();
        }
    };

    for (size_t i = 0; i < Base::numTasks; i++) {
        Entry &e = this->tasks[i];
        e.target = tasks[i];
        auto h = handler(i);
        if (e.target) {
            // If was not instantly released in handler.
            e.handler = h;
        }
    }
}

template <size_t NumTasks>
bool
AllTasksAwaiter<NumTasks>::await_suspend(Task::CoroutineHandle handle)
{
    if (numLeft == 0) {
        return false;
    }

    Base::waiter = handle;
    // Handlers should have the same priority as waiter.
    Task::Priority pri = Base::waiter.GetPromise().priority;
    for (size_t i = 0; i < Base::numTasks; i++) {
        Entry &e = this->tasks[i];
        e.handler.SetPriority(pri);
    }

    return true;
}

template <size_t NumTasks>
AnyTaskAwaiter<NumTasks>::AnyTaskAwaiter(const etl::span<Task, NumTasks> &tasks):
    details::MultipleTasksAwaiter<NumTasks>(tasks)
{
    auto handler = [this](size_t index) -> Awaitable<void> {
        Entry &e = this->tasks[index];

        co_await e.target;

        e.target.ReleaseHandle();
        e.handler.ReleaseHandle();

        if (result != NONE) {
            co_return;
        }
        result = index;
        if (Base::waiter) {
            Base::waiter.Schedule();
        }
    };

    for (size_t i = 0; i < Base::numTasks; i++) {
        Entry &e = this->tasks[i];
        e.target = tasks[i];
        auto h = handler(i);
        if (e.target) {
            // If was not instantly released in handler.
            e.handler = h;
        } else {
            PULSE_ASSERT(result != NONE);
            Base::Finish();
            break;
        }
    }
}

template <size_t NumTasks>
bool
AnyTaskAwaiter<NumTasks>::await_suspend(Task::CoroutineHandle handle)
{
    if (result != NONE) {
        return false;
    }
    Base::waiter = handle;

    // Handlers should have the same priority as waiter.
    Task::Priority pri = Base::waiter.GetPromise().priority;
    for (size_t i = 0; i < Base::numTasks; i++) {
        Entry &e = this->tasks[i];
        e.handler.SetPriority(pri);
    }

    return true;
}


} // namespace pulse

// Bind TaskPromise to Task coroutine type.
template<typename TRet, bool initialSuspend, typename... Args>
struct std::coroutine_traits<pulse::TTask<TRet, initialSuspend>, Args...> {
    using promise_type = pulse::TTask<TRet, initialSuspend>::TPromise;
};

#endif /* TASK_H */
