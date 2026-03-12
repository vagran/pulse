#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <pulse/details/default_config.h>
#include <pulse/details/common.h>
#include <pulse/coroutine.h>
#include <etl/bit.h>


namespace pulse {

class TaskPromise;

template <typename TRet, bool initialSuspend>
class TTaskPromise;


/// Smart pointer for coroutine frame.
class Task {
public:
    using TPromise = TaskPromise;

    static constexpr int NUM_PRIO_BITS = BitWidth(pulseConfig_NUM_TASK_PRIORITIES - 1);

    using Priority = uint8_t;

    static constexpr Priority HIGHEST_PRIORITY = 0,
                              ISR_PRIORITY = HIGHEST_PRIORITY,
                              LOWEST_PRIORITY = pulseConfig_NUM_TASK_PRIORITIES - 1;

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

    /** Spawns new task with the specified priority. Returns ID_NONE if failed (if
     * `pulseConfig_PANIC_ON_TASK_SPAWN_FAILURE` disabled, panics otherwise).
     * @param task Task object returned by coroutine function.
     * @param priority Task priority.
     */
    static const Task &
    Spawn(Task task, Priority priority = LOWEST_PRIORITY);

    /**
     * Run main loop of tasks scheduler. Never returns.
     */
    static void
    RunScheduler();

protected:
    friend class TaskPromise;
    template <typename, bool>
    friend class TTaskPromise;

    using CoroutineHandle = std::coroutine_handle<TaskPromise>;

    CoroutineHandle handle;

    inline
    Task(CoroutineHandle handle);

    inline void
    ReleaseHandle();
};

template <typename TRet, bool initialSuspend = true>
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

class TaskPromise {
public:
    /// Next task when in list, none if last one.
    Task next = nullptr;
    uint8_t refCounter = 0;
    uint8_t priority: Task::NUM_PRIO_BITS = Task::LOWEST_PRIORITY,
            isRunnable: 1 = 0;


    TaskPromise() = default;

    ~TaskPromise();

    /// Add reference from new Task instance.
    void
    AddRef();

    /// Release reference from Task instance.
    /// @return True if last reference released.
    bool
    ReleaseRef()
    {
        PULSE_ASSERT(refCounter != 0);
        return --refCounter == 0;
    }

    std::suspend_never
    final_suspend() noexcept
    {
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
        //XXX
    }

    //XXX design yield semantic
    // template<std::convertible_to<T> From>
    // std::suspend_always
    // yield_value(From&& from)
    // {
    //     value.emplace(std::forward<From>(from));
    //     return {};
    // }
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
    {}
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

/// For returning from async functions net meant to be spawned as scheduler tasks.
template <typename TRet>
using Awaitable = TTask<TRet, false>;

// Singly-linked list.
struct TaskList {
    Task head = nullptr;

    void
    AddFirst(const Task &task);
};

// Singly-linked list with tail pointer.
struct TaskTailedList {
    Task head = nullptr,
         tail = nullptr;

    void
    AddFirst(const Task &task);

    void
    AddLast(const Task &task);
};


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
