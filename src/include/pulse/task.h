#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <pulse/details/default_config.h>
#include <pulse/details/common.h>
#include <pulse/coroutine.h>
#include <etl/bit.h>


namespace pulse {

template <typename TRet = void>
class TTask;

using Task = TTask<void>;

class TaskPromise;

template <typename TRet>
class TTaskPromise;


/// Smart pointer for coroutine frame.
template <>
class TTask<void> {
public:
    // ID is one-based index in tasks array. Zero is reserved for ID_NONE.
    using Id = SizedUint<UintBitWidth(pulseConfig_MAX_TASKS)>;

    static constexpr Id ID_NONE = 0;

    static constexpr int NUM_PRIO_BITS = BitWidth(pulseConfig_NUM_TASK_PRIORITIES - 1);

    using Priority = uint8_t;

    static constexpr Priority HIGHEST_PRIORITY = 0,
                              ISR_PRIORITY = HIGHEST_PRIORITY,
                              LOWEST_PRIORITY = pulseConfig_NUM_TASK_PRIORITIES - 1;

    TTask() = default;

    TTask(const Task &other);

    TTask(Task &&other) noexcept:
        handle(other.handle)
    {
        other.handle = CoroutineHandle();
    }

    ~TTask();

    TaskPromise &
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

    /// @return Task ID if managed by scheduler, ID_NONE otherwise.
    Id
    GetId() const;

    /// @return Task corresponding to the given ID. Null task if not found.
    static Task
    ById(Id id);

    /** Spawns new task with the specified priority. Returns ID_NONE if failed (if
     * `pulseConfig_PANIC_ON_TASK_SPAWN_FAILURE` disabled, panics otherwise).
     * @param task Task object returned by coroutine function.
     * @param priority Task priority.
     */
    static Id
    Spawn(Task task, Priority priority = LOWEST_PRIORITY);

    /**
     * Run main loop of tasks scheduler. Never returns.
     */
    static void
    RunScheduler();

private:
    friend class TaskPromise;

    using CoroutineHandle = std::coroutine_handle<TaskPromise>;

    CoroutineHandle handle;

    TTask(CoroutineHandle handle);
};

template <typename TRet>
class TTask: public Task {
public:
    //XXX awaitable, template (returned value, yielded values)
    int
    Join();
};

class TaskPromise {
public:
    Task::Id id = Task::ID_NONE;
    /// Next task when in list, none if last one.
    Task::Id next = Task::ID_NONE;
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

    Task
    get_return_object()
    {
        return Task(Task::CoroutineHandle::from_promise(*this));
    }

    // Coroutine body is first entered only by scheduler.
    std::suspend_always
    initial_suspend()
    {
        return {};
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

    //XXX design yield semantic
    // template<std::convertible_to<T> From>
    // std::suspend_always
    // yield_value(From&& from)
    // {
    //     value.emplace(std::forward<From>(from));
    //     return {};
    // }

    //XXX design value return semantic, set join return value


};

template <>
class TTaskPromise<void>: public TaskPromise {
public:
    //XXX awaitable for join

    void
    return_void()
    {}
};

template <typename TRet>
class TTaskPromise: public TaskPromise {
public:
    //XXX awaitable for join

    template<etl::convertible_to<TRet> From>
    void
    return_value(From&& from)
    {
        //XXX
    }
};


// Singly-linked list.
struct TaskList {
    Task::Id head = Task::ID_NONE;

    void
    AddFirst(Task::Id taskId);
};

// Singly-linked list with tail pointer.
struct TaskTailedList {
    Task::Id head = Task::ID_NONE,
           tail = Task::ID_NONE;

    void
    AddFirst(Task::Id taskId);

    void
    AddLast(Task::Id taskId);
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
    using promise_type = pulse::TTaskPromise<TRet>;
};

#endif /* TASK_H */
