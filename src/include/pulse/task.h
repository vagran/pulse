#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <pulse/details/default_config.h>
#include <pulse/details/common.h>
#include <pulse/coroutine.h>
#include <pulse/list.h>
#include <etl/bit.h>


namespace pulse {

class TaskPromise;

template <typename TRet, bool initialSuspend>
class TTaskPromise;


template <typename TRet, bool initialSuspend = true>
class TTask;

class TaskSwitchAwaiter;


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

        void
        await_resume() const
        {}
    };


    Task() = default;

    Task(etl::nullptr_t):
        Task()
    {}

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

    /** Enqueue this task into ready tasks queue according to its current priority. */
    void
    Schedule() const &;

    void
    Schedule() &&;

    //XXX Terminate()?

    template <typename TRet, bool initialSuspend>
    static TTask<TRet, initialSuspend>
    Spawn(TTask<TRet, initialSuspend> task, Priority priority = LOWEST_PRIORITY)
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

    /** Switch to other runnable task if any.
     * @code
     * co_await Task::Switch();
     * @endcode
     */
    static TaskSwitchAwaiter
    Switch()
    {
        return {};
    }

protected:
    friend class TaskPromise;
    template <typename, bool>
    friend class TTaskPromise;

    CoroutineHandle handle;

    inline
    Task(CoroutineHandle handle);

    inline void
    ReleaseHandle();

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
};

using TaskV = TTask<void>;


/** Also acts as task control block. */
class TaskPromise {
public:
    /// Next task when in list, none if last one.
    Task next = nullptr;
    uint8_t refCounter = 0;
    uint8_t priority: Task::NUM_PRIO_BITS = Task::LOWEST_PRIORITY,
            isRunnable: 1 = 0,//XXX is needed?
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
};


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
            GetResult().~TRet();
        }
    }

    etl::conditional_t<initialSuspend, std::suspend_always, std::suspend_never>
    initial_suspend()
    {
        //XXX schedule in suspend_always::await_suspend()? priority?
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
        new (result.data) TRet(std::forward<From>(from));
        //XXX awaiters
    }

    const TRet &
    GetResult() const
    {
        PULSE_ASSERT(isFinished);
        return *reinterpret_cast<const TRet *>(result.data);
    }

    //XXX design yield semantic
    // template<std::convertible_to<T> From>
    // std::suspend_always
    // yield_value(From&& from)
    // {
    //     value.emplace(std::forward<From>(from));
    //     return {};
    // }
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
        //XXX awaiters
    }
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


namespace details {

inline TaskPromise &
GetTaskPromise(const Task &task)
{
    return task.GetPromise();
}

} // namespace details

using TaskList = List<Task, details::GetTaskPromise>;
using TaskTailedList = TailedList<Task, details::GetTaskPromise>;


/// For returning from async functions not meant to be spawned as scheduler tasks.
template <typename TRet>
using Awaitable = TTask<TRet, false>;


template <typename T = void>
class Awaiter;

template <>
class Awaiter<void> {
public:
    TaskList waitingTasks;

    //XXX
};

template <typename T>
class Awaiter: public Awaiter<void> {
public:
    //XXX
};

} // namespace pulse

// Bind TaskPromise to Task coroutine type.
template<typename TRet, typename... Args>
struct std::coroutine_traits<pulse::TTask<TRet>, Args...> {
    using promise_type = pulse::TTask<TRet>::TPromise;
};

#endif /* TASK_H */
