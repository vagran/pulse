#ifndef TASK_H
#define TASK_H

#include <pulse/config.h>
#include <pulse/details/common.h>
#include <pulse/coroutine.h>
#include <pulse/list.h>
#include <pulse/shared_ptr.h>

#include <etl/memory.h>
#include <etl/span.h>
#include <etl/invoke.h>


namespace pulse {

class TaskPromise;

template <typename TRet, bool initialSuspend>
class TTaskPromise;

template <typename TRet, bool initialSuspend = true>
class TTask;

/// Task returning void result.
using TaskV = TTask<void>;

/// Return this type from non-top-level async functions not meant to be spawned as scheduler tasks.
template <typename TRet>
using Awaitable = TTask<TRet, false>;

class TaskSwitchAwaiter;

template <typename TRet>
class TaskAwaiter;

template <size_t NumTasks = etl::dynamic_extent>
class AllTasksAwaiter;

template <size_t NumTasks = etl::dynamic_extent>
class AnyTaskAwaiter;


namespace details {

/// Function suitable for spawning tasks.
template <typename F, typename TRet, typename... Args>
concept AsyncTaskFunction = etl::is_invocable_r_v<TTask<TRet>, F, Args...>;

template <typename T>
struct TaskTraits {
    static constexpr bool isTask = false;
};

template <typename TRet_, bool initialSuspend_>
struct TaskTraits<TTask<TRet_, initialSuspend_>> {
    static constexpr bool isTask = true;

    using TRet = TRet_;
    static constexpr bool initialSuspend = initialSuspend_;
};

class TaskAwaiterBase;

class TaskWeakPtr;
class TaskWeakPtrTag;

} // namespace details


/// Smart pointer for coroutine frame. At least one reference should be kept until task is finished
/// otherwise it might be destroyed earlier.
class Task {
public:
    using TPromise = TaskPromise;

    static constexpr int NUM_PRIO_BITS = BitWidth(pulseConfig_NUM_TASK_PRIORITIES - 1);

    using Priority = uint8_t;

    static constexpr Priority HIGHEST_PRIORITY = 0,
                              ISR_PRIORITY = HIGHEST_PRIORITY,
                              LOWEST_PRIORITY = pulseConfig_NUM_TASK_PRIORITIES - 1;

    using CoroutineHandle = std::coroutine_handle<TaskPromise>;

    /** Does not prevent task from destruction when last reference from `Task` handle is released.
     */
    using WeakPtr = details::TaskWeakPtr;


    Task() = default;

    Task(etl::nullptr_t):
        Task()
    {}

    inline
    Task(CoroutineHandle handle);

    inline
    Task(const Task &other);

    Task(Task &&other):
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
    operator =(Task &&other);

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

    /** Get weak pointer to this task. This requires dynamic allocation for the first call for the
     * given co-routine instance.
     */
    inline WeakPtr
    GetWeakPtr();

    /** Enqueue this task into ready tasks queue according to its current priority. */
    void
    Schedule() const &;

    void
    Schedule() &&;

    inline bool
    IsFinished() const;

    void
    SetPriority(Priority priority);

    /** Pass the task to the scheduler. The returned reference should be kept until task is finished
     * otherwise it might be destroyed once the first suspension point is reached (scheduler
     * releases its reference and awaiters are not allowed to store task reference to prevent
     * reference loop).
     */
    template <typename TRet>
    static TTask<TRet, true>
    Spawn(TTask<TRet, true> task, Priority priority = LOWEST_PRIORITY)
    {
        SpawnImpl(task, priority);
        return etl::move(task);
    }

    /** Spawn new task by providing task function.
     * @return Created task.
     */
    template <typename F, typename... Args>
    requires details::AsyncTaskFunction<F,
        typename details::TaskTraits<etl::invoke_result_t<F, Args...>>::TRet, Args...>
    static auto
    Spawn(F &&func, Args &&... args)
    {
        auto task = etl::invoke(etl::forward<F>(func), etl::forward<Args>(args)...);
        SpawnImpl(task, LOWEST_PRIORITY);
        return etl::move(task);
    }

    /** Spawn new task by providing task function.
     * @return Created task.
     */
    template <typename F, typename... Args>
    requires details::AsyncTaskFunction<F,
        typename details::TaskTraits<etl::invoke_result_t<F, Args...>>::TRet, Args...>
    static auto
    Spawn(F &&func, Priority priority, Args &&... args)
    {
        auto task = etl::invoke(etl::forward<F>(func), etl::forward<Args>(args)...);
        SpawnImpl(task, priority);
        return etl::move(task);
    }

    /** Spawn new task by providing synchronous function. The function is called when task is run by
     * the scheduler. Can be used to defer the call, e.g. from ISR.
     * @return Created task.
     */
    template <typename F, typename... Args>
    requires etl::is_invocable_v<F, Args...> &&
        (!details::TaskTraits<etl::invoke_result_t<F, Args...>>::isTask)
    static auto
    Spawn(F &&func, Args &&... args)
    {
        using TRet = etl::remove_cvref_t<etl::invoke_result_t<F, Args...>>;

        auto taskFunc = [](F &&func, Args &&... args) -> TTask<TRet> {
            co_return etl::invoke(etl::forward<F>(func), etl::forward<Args>(args)...);
        };

        auto task = taskFunc(etl::forward<F>(func), etl::forward<Args>(args)...);
        SpawnImpl(task, LOWEST_PRIORITY);
        return etl::move(task);
    }

    /** Spawn new task by providing synchronous function. The function is called when task is run by
     * the scheduler. Can be used to defer the call, e.g. from ISR.
     * @return Created task.
     */
    template <typename F, typename... Args>
    requires etl::is_invocable_v<F, Args...> &&
        (!details::TaskTraits<etl::invoke_result_t<F, Args...>>::isTask)
    static auto
    Spawn(F &&func, Priority priority, Args &&... args)
    {
        using TRet = etl::remove_cvref_t<etl::invoke_result_t<F, Args...>>;

        auto taskFunc = [](F &&func, Args &&... args) -> TTask<TRet> {
            co_return etl::invoke(etl::forward<F>(func), etl::forward<Args>(args)...);
        };

        auto task = taskFunc(etl::forward<F>(func), etl::forward<Args>(args)...);
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
    static inline TaskSwitchAwaiter
    Switch();

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

    /** Wait when all of the provided tasks complete. Arguments may either be tasks or any awaiter
     * objects. Awaiter objects should have lifetime at least until return awaiter is resumed.
     */
    template <class T1, class T2, class... Args>
    static AllTasksAwaiter<sizeof...(Args) + 2>
    WhenAll(T1 &&task1, T2 &&task2, Args &&... tasks);

    /** Wait when any of the provided tasks completes. Arguments may either be tasks or any awaiter
     * objects. Awaiter objects should have lifetime at least until return awaiter is resumed.
     */
    template <class T1, class T2, class... Args>
    static AnyTaskAwaiter<sizeof...(Args) + 2>
    WhenAny(T1 &&task1, T2 &&task2, Args &&... tasks);

    /** Helper to wrap awaiters into task. Can also accept Task or TTask and return it as is.
     * Passed awaiter lifetime should be at least until the returned task is finished (awaiter
     * resumes).
     */
    template <class T>
    static inline Task
    Make(T &&obj);

protected:
    friend class TaskPromise;

    template <typename, bool>
    friend class TTaskPromise;

    friend class details::TaskAwaiterBase;

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

    /// @return True if added to wait list, false if already finished.
    bool
    AwaitResult(details::TaskAwaiterBase *waiter, const Task &task) const;

    void
    CancelAwaitResult(details::TaskAwaiterBase *waiter) const;
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


namespace details {

struct TaskListTrait {
    static inline Task
    GetNext(const Task &p);

    template <typename TPtr>
    static inline void
    SetNext(Task &p, TPtr &&next);
};

} // namespace details

using TaskList = List<Task, details::TaskListTrait>;
using TaskTailedList = TailedList<Task, details::TaskListTrait>;


/** Also acts as task control block. */
class TaskPromise {
public:
    /// Next task when in list, none if last one.
    Task next = nullptr;
    /// Tag for weak pointers if any created.
    SharedPtr<details::TaskWeakPtrTag> weakPtrTag = nullptr;
    /// Awaiters currently awaiting this task finishing.
    List<details::TaskAwaiterBase *> resultWaiters;
    etl::atomic<uint8_t> refCounter = 0;
    uint8_t priority: Task::NUM_PRIO_BITS = Task::LOWEST_PRIORITY,
    /// Task currently queued in runnable queue.
            isRunnable: 1 = 0,
    /// Task finished and result is available.
            isFinished: 1 = 0;

    TaskPromise() = default;

    // No need to make it virtual since promise object is always constructed and destructed from
    // coroutine frame constructor/destructor by concrete type.
    ~TaskPromise();

    /// Add reference from new Task instance.
    void
    AddRef()
    {
        auto prevValue = refCounter.fetch_add(1);
        if (prevValue == etl::numeric_limits<decltype(prevValue)>::max()) {
            PULSE_PANIC("Task reference counter overflow");
        }
    }

    /// Release reference from Task instance.
    /// @return True if last reference released.
    bool
    ReleaseRef()
    {
        auto prevValue = refCounter.fetch_sub(1);
        PULSE_ASSERT(prevValue != 0);
        return prevValue == 1;
    }

    Task::WeakPtr
    GetWeakPtr();

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


namespace details {

class TaskWeakPtrTag {
public:
    TaskWeakPtrTag(Task::CoroutineHandle handle):
        handle(handle)
    {}

    TaskWeakPtrTag(const TaskWeakPtrTag &) = delete;

    // Null if co-routine destroyed.
    Task::CoroutineHandle handle;

private:
    friend struct details::SharedPtrDefaultAtomicTrait<TaskWeakPtrTag>;

    etl::atomic<uint8_t> refCounter = 0;
};

class TaskWeakPtr {
public:
    TaskWeakPtr() = default;
    TaskWeakPtr(const TaskWeakPtr &) = default;
    TaskWeakPtr(TaskWeakPtr &&) = default;

    TaskWeakPtr(etl::nullptr_t):
        tag(nullptr)
    {}

    TaskWeakPtr &
    operator =(const TaskWeakPtr &other) = default;

    TaskWeakPtr &
    operator =(TaskWeakPtr &&other) = default;

    /** This does not guarantee the referenced task is still alive, use Lock() to check it. It just
     * checks it tag is attached which might be useful in some cases.
     */
    operator bool() const
    {
        return tag;
    }

    /** Obtain reference to task if not yet destroyed. Returns null (empty Task) if already
     * destroyed.
     */
    Task
    Lock();

    /// Make it null.
    void
    Reset()
    {
        tag.Reset();
    }

    /** Locks referenced task, resets this pointer and schedules the task if any. This is typical
     * pattern used in awaiters.
     */
    void
    Wakeup()
    {
        Task t = Lock();
        Reset();
        if (t) {
            etl::move(t).Schedule();
        }
    }

private:
    friend class pulse::TaskPromise;

    using TagPtr = SharedPtr<TaskWeakPtrTag>;

    TagPtr tag;

    TaskWeakPtr(TagPtr tag):
        tag(etl::move(tag))
    {}
};

} // namespace details


/** For now it is mostly marker interface. There is currently no need to make it polymorphic. */
template <typename TRet>
class Awaiter {
public:
    /* Each awaiter should follow some basic rules to make it properly functional and robust.
     *
     * 1. Awaiter should have destructor which should cancel any queued operations.
     *      Awaiter may be created but never awaited, or, more common, it can be used in
     *      `Task::WhenAny()` so that some awaiters are never resumed and are destructed after
     *      suspend but before resume. All these cases should be properly handled by awaiter
     *      destructor. Event source should have API to cancel queued awaiter. This also allows
     *      safe destroying of suspended tasks.
     *
     * 2. Awaiter should queue `this` pointer to an event source, not Task (reason in bullet 3).
     *      This implies restriction on moving awaiter instance after suspended. For simplicity,
     *      just disable copy and move constructors for all awaiters, this work well for most cases.
     *
     * 3. Awaiter should never store suspended task handle, nor queue it to an event source, to
     *      prevent reference loop. Suspended task may be destructed when the last reference
     *      released, but the very last reference may be held by an awaiter which in turn is
     *      destructed when coroutine frame is destructed, which never happens in such case. This is
     *      specifically important for `Task::WhenAny()` case, which will release references to all
     *      non-finished tasks, which should cause coroutine frame destruction, and thus, awaiter
     *      destruction and cancelling all queued operations (see bullet 1). Holding reference to
     *      coroutine frame by `Task` instance prevents this, and causes the queued operation and
     *      dangling coroutine to resume at some point, causing undesired effects (and consuming
     *      memory while dangling). Use `Task::WeakPtr` to store suspended task reference in an
     *      awaiter.
     */

    Awaiter() = default;
    Awaiter(const Awaiter &) = delete;
    Awaiter(Awaiter &&) = delete;

    /* These methods should be implemented in derived class:
     *
     * bool
     * await_ready() [const];
     *
     * bool | void
     * await_suspend(Task::CoroutineHandle handle);
     *
     * TRet
     * await_resume() [const];
     */
};


// Awaiter for explicit task switching.
class TaskSwitchAwaiter: public Awaiter<void> {
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


namespace details {

class TaskAwaiterBase {
public:
    TaskAwaiterBase(Task task):
        task(etl::move(task))
    {}

    ~TaskAwaiterBase()
    {
        if (waiter) {
            task.CancelAwaitResult(this);
        }
    }

    bool
    await_ready() const
    {
        return task.IsFinished();
    }

    bool
    await_suspend(Task::CoroutineHandle handle)
    {
        Task waiterTask(handle);
        if (!task.AwaitResult(this, waiterTask)) {
            return false;
        }
        waiter = waiterTask.GetWeakPtr();
        return true;
    }

protected:
    friend struct details::ListDefaultTrait<TaskAwaiterBase *>;
    friend class pulse::TaskPromise;

    const Task task;
    TaskAwaiterBase *next = nullptr;
    Task::WeakPtr waiter;
};

} // namespace details

template <typename TRet>
class TaskAwaiter: public details::TaskAwaiterBase, public Awaiter<TRet> {
public:
    template <bool initialSuspend>
    TaskAwaiter(TTask<TRet, initialSuspend> task):
        TaskAwaiterBase(etl::move(task)),
        initialSuspend(initialSuspend)
    {}

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
    const bool initialSuspend;
};


template <>
class TaskAwaiter<void>: public details::TaskAwaiterBase, public Awaiter<void> {
public:
    TaskAwaiter(Task task):
        TaskAwaiterBase(etl::move(task))
    {}

    void
    await_resume() const
    {}
};


namespace details {

class MultipleTasksAwaiterBase {
protected:
    struct Entry {
        Task target, handler;
    };

    MultipleTasksAwaiterBase() = default;

    Task::WeakPtr waiter;

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
class AllTasksAwaiter: public details::MultipleTasksAwaiter<NumTasks>, public Awaiter<void> {
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
    using Entry = typename Base::Entry;

    size_t numLeft;
};


template <size_t NumTasks>
class AnyTaskAwaiter: public details::MultipleTasksAwaiter<NumTasks>, public Awaiter<size_t>  {
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
    using Entry = typename Base::Entry;

    static constexpr size_t NONE = etl::numeric_limits<size_t>::max();

    size_t result = NONE;
};

// /////////////////////////////////////////////////////////////////////////////////////////////////

Task::Task(CoroutineHandle handle):
    handle(handle)
{
    if (handle) {
        GetPromise().AddRef();
    }
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
Task::operator =(Task &&other)
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

TaskSwitchAwaiter
Task::Switch()
{
    return {};
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

template <class T1, class T2, class... Args>
AllTasksAwaiter<sizeof...(Args) + 2>
Task::WhenAll(T1 &&task1, T2 &&task2, Args &&... tasks)
{
    Task _tasks[] = {
        Task::Make(etl::forward<T1>(task1)),
        Task::Make(etl::forward<T2>(task2)),
        Task::Make(etl::forward<Args>(tasks))...
    };
    return WhenAll(etl::span<Task, sizeof...(Args) + 2>(_tasks));
}

template <class T1, class T2, class... Args>
AnyTaskAwaiter<sizeof...(Args) + 2>
Task::WhenAny(T1 &&task1, T2 &&task2, Args &&... tasks)
{
    Task _tasks[] = {
        Task::Make(etl::forward<T1>(task1)),
        Task::Make(etl::forward<T2>(task2)),
        Task::Make(etl::forward<Args>(tasks))...
    };
    return WhenAny(etl::span<Task, sizeof...(Args) + 2>(_tasks));
}

namespace details {

template <class T>
struct AwaitableWrapper {
    static inline Awaitable<void>
    MakeTask(const T &obj)
    {
        // Passed awater lifetime should not end before the task completion, so assuming it is
        // safe. `await_suspend()` requires non-const reference in most cases.
        co_await const_cast<T &>(obj);
    }
};

template <typename T>
concept TaskType = etl::derived_from<T, Task>;

template <TaskType T>
struct AwaitableWrapper<T> {
    template <class U>
    static inline Task
    MakeTask(U &&obj)
    {
        return etl::forward<U>(obj);
    }
};

} // namespace details

template <class T>
Task
Task::Make(T &&obj)
{
    return details::AwaitableWrapper<etl::remove_cvref_t<T>>::MakeTask(etl::forward<T>(obj));
}

void
Task::ReleaseHandle()
{
    if (handle) {
        auto h = handle;
        if (GetPromise().ReleaseRef()) {
            // Change state first to make iti visible to coroutine frame destructors
            handle = CoroutineHandle();
            h.destroy();
        } else {
            handle = CoroutineHandle();
        }
    }
}

Task::WeakPtr
Task::GetWeakPtr()
{
    if (!handle) {
        return nullptr;
    }
    return GetPromise().GetWeakPtr();
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


Task
details::TaskListTrait::GetNext(const Task &p)
{
    return p.GetPromise().next;
}

template <typename TPtr>
void
details::TaskListTrait::SetNext(Task &p, TPtr &&next)
{
    p.GetPromise().next = etl::forward<TPtr>(next);
}


template <size_t NumTasks>
AllTasksAwaiter<NumTasks>::AllTasksAwaiter(const etl::span<Task, NumTasks> &tasks):
    details::MultipleTasksAwaiter<NumTasks>(tasks),
    numLeft(Base::numTasks)
{
    // It is observed that just capturing `this` by value (like `[this]`) may not work in GCC for
    // some reason - captured `this` value is not preserved across suspension point. At the same
    // time it works flawlessly in Clang. So use workaround here with `self` argument.
    auto handler = [](size_t index, decltype(this) self) -> Awaitable<void> {
        Entry &e = self->tasks[index];

        co_await e.target;

        e.target.ReleaseHandle();
        e.handler.ReleaseHandle();

        self->numLeft--;
        if (self->numLeft == 0 && self->waiter) {
            self->waiter.Wakeup();
        }
    };

    for (size_t i = 0; i < Base::numTasks; i++) {
        Entry &e = this->tasks[i];
        e.target = tasks[i];
        PULSE_ASSERT(e.target);
        auto h = handler(i, this);
        if (e.target) {
            // If was not instantly released in handler.
            e.handler = etl::move(h);
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

    Task waiterTask(handle);
    Base::waiter = waiterTask.GetWeakPtr();

    // Handlers should have the same priority as waiter.
    Task::Priority pri = waiterTask.GetPromise().priority;
    for (size_t i = 0; i < Base::numTasks; i++) {
        Entry &e = this->tasks[i];
        if (e.handler) {
            e.handler.SetPriority(pri);
        }
    }

    return true;
}

template <size_t NumTasks>
AnyTaskAwaiter<NumTasks>::AnyTaskAwaiter(const etl::span<Task, NumTasks> &tasks):
    details::MultipleTasksAwaiter<NumTasks>(tasks)
{
    // It is observed that just capturing `this` by value (like `[this]`) may not work in GCC for
    // some reason - captured `this` value is not preserved across suspension point. At the same
    // time it works flawlessly in Clang. So use workaround here with `self` argument.
    auto handler = [](size_t index, decltype(this) self) -> Awaitable<void> {
        Entry &e = self->tasks[index];

        co_await e.target;

        e.target.ReleaseHandle();
        e.handler.ReleaseHandle();

        if (self->result != NONE) {
            co_return;
        }
        self->result = index;
        if (self->waiter) {
            self->waiter.Wakeup();
        }
    };

    for (size_t i = 0; i < Base::numTasks; i++) {
        Entry &e = this->tasks[i];
        e.target = tasks[i];
        PULSE_ASSERT(e.target);
        auto h = handler(i, this);
        if (e.target) {
            // If was not instantly released in handler.
            e.handler = etl::move(h);
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

    Task waiterTask(handle);
    Base::waiter = waiterTask.GetWeakPtr();

    // Handlers should have the same priority as waiter.
    Task::Priority pri = waiterTask.GetPromise().priority;
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
    using promise_type = typename pulse::TTask<TRet, initialSuspend>::TPromise;
};

#endif /* TASK_H */
