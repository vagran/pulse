#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <pulse/details/default_config.h>
#include <pulse/details/common.h>
#include <pulse/coroutine.h>


namespace pulse {

class TaskPromise;

class Task {
public:
    using Id = SizedUint<FitUintBits(pulseConfig_MAX_TASKS + 1)>;

    static constexpr Id ID_NONE = 0;

    using Priority = uint8_t;

    static constexpr Priority HIGHEST_PRIORITY = 0,
                              ISR_PRIORITY = HIGHEST_PRIORITY,
                              LOWEST_PRIORITY = pulseConfig_NUM_TASK_PRIORITIES - 1;

    Task(const Task &) = delete;

    Task(Task &&other) noexcept:
        handle(other.handle)
    {
        other.handle = CoroutineHandle();
    }

    ~Task()
    {
        if (handle) {
            handle.destroy();
        }
    }

    /** Spawns new task with the specified priority. Returns ID_NONE if failed (if
     * `pulseConfig_PANIC_ON_TASK_SPAWN_FAILURE` disabled, panics otherwise).
     */
    static Id
    Spawn(Task &&task, Priority priority);

    /**
     * Run main loop of tasks scheduler. Never returns.
     */
    static void
    RunScheduler();

private:
    friend class TaskPromise;

    using CoroutineHandle = std::coroutine_handle<TaskPromise>;

    CoroutineHandle handle;

    Task(CoroutineHandle handle):
        handle(handle)
    {}
};

class TaskPromise {
public:
    Task::Id id = Task::ID_NONE;
    Task::Priority priority = Task::LOWEST_PRIORITY;

    TaskPromise() = default;

    TaskPromise &
    FromTask(Task &task)
    {
        return task.handle.promise();
    }

    Task
    get_return_object()
    {
        return Task(Task::CoroutineHandle::from_promise(*this));
    }

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

    //XXX design value return semantic

    void
    return_void()
    {}
};

} // namespace pulse

// Bind TaskPromise to Task coroutine type.
template<typename... Args>
struct std::coroutine_traits<pulse::Task, Args...> {
    using promise_type = pulse::TaskPromise;
};

#endif /* TASK_H */
