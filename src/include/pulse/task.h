#ifndef TASK_H
#define TASK_H

#include <pulse/config.h>
#include <pulse/details/common.h>
#include <pulse/coroutine.h>
#include <pulse/list.h>
#include <pulse/shared_ptr.h>
#include <pulse/port.h>

#include <etl/optional.h>
#include <etl/span.h>
#include <etl/invoke.h>


namespace pulse {


namespace details {

/// Task control block associated with every task/awaitable.
struct TaskCb;

/// Promise type for coroutines.
class TaskPromiseBase;

template <typename TRet>
class TypedTaskPromise;

template <typename TRet, bool initialSuspend>
class TaskPromise;

/// Base class for awaiter for task result.
class TaskAwaiterBase;


struct TaskCbSharedPtrTrait: public details::SharedPtrDefaultAtomicTrait<TaskCb> {
    static inline void
    Delete(TaskCb &obj);
};

using TaskCbPtr = SharedPtr<details::TaskCb, details::TaskCbSharedPtrTrait>;

class TaskImpl;

} // namespace details


namespace tasks {

static constexpr int NUM_PRIO_BITS = BitWidth(pulseConfig_NUM_TASK_PRIORITIES - 1);

using Priority = uint8_t;

static constexpr Priority HIGHEST_PRIORITY = 0,
                          LOWEST_PRIORITY = pulseConfig_NUM_TASK_PRIORITIES - 1;

using CoroutineHandle = std::coroutine_handle<details::TaskPromiseBase>;

} // namespace tasks

class TaskWeakRef;

class TaskSwitchAwaiter;

template <typename TRet>
class TaskAwaiter;


namespace details {

class TaskRefBase {
public:
    TaskRefBase() = default;

    TaskRefBase(const TaskRefBase &) = default;
    TaskRefBase(TaskRefBase &&) = default;

    TaskRefBase(etl::nullptr_t):
        cb(nullptr)
    {}

    TaskRefBase &
    operator =(const TaskRefBase &other) = default;

    TaskRefBase &
    operator =(TaskRefBase &&other) = default;

    bool
    operator ==(const TaskRefBase &other) const
    {
        return cb == other.cb;
    }

    bool
    operator !=(const TaskRefBase &other) const
    {
        return cb != other.cb;
    }

    /// @return True if bound to a control block.
    operator bool() const
    {
        return cb;
    }

    inline bool
    IsFinished() const;

    inline tasks::Priority
    GetPriority() const;

protected:
    details::TaskCbPtr cb;

    TaskRefBase(details::TaskCbPtr cb):
        cb(etl::move(cb))
    {}

    TaskRefBase(details::TaskCb *cb):
        cb(cb)
    {}
};

} // namespace details


/// Task handle. Task is destroyed when last handle is destroyed (if the task is not pinned).
class TaskRef: public details::TaskRefBase {
public:

    TaskRef() = default;

    inline
    TaskRef(const TaskRef &other);

    TaskRef(TaskRef &&other) = default;

    TaskRef(tasks::CoroutineHandle coro);

    ~TaskRef()
    {
        ReleaseHandle();
    }

    inline TaskRef &
    operator =(const TaskRef &other);

    inline TaskRef &
    operator =(TaskRef &&other);

    /// Clear associated handle, making this task null.
    void
    ReleaseHandle();

    /// Pin task so that it is not destructed if last reference released.
    inline TaskRef &
    Pin();

    /** Unpin task if was previously pinned by `Pin()` method.
     *
     * @return True if task did not have references, was by pinning only, and was destroyed after
     *  unpinning.
     */
    bool
    Unpin() const;

    /** Get weak reference to this task. */
    inline TaskWeakRef
    GetWeakPtr() const;

    /** Enqueue this task into ready tasks queue according to its current priority. Can be called
     * from ISR.
     */
    void
    Schedule() const;

    inline void
    SetPriority(tasks::Priority priority) const;

    /// Set new priority if it is higher than current task priority.
    inline void
    RaisePriority(tasks::Priority priority) const;

    inline TaskAwaiter<void>
    Wait() const;

    inline TaskAwaiter<void>
    operator co_await() const;

protected:
    template<typename TRet, bool initialSuspend>
    friend class details::TaskPromise;

    friend struct details::TaskCb;
    friend class details::TaskImpl;
    friend class TaskWeakRef;
    friend class TaskSwitchAwaiter;
    friend class details::TaskAwaiterBase;
    friend class details::TaskPromiseBase;

    template<typename TRet>
    friend class TaskAwaiter;


    TaskRef(details::TaskCbPtr cb):
        TaskRefBase(etl::move(cb))
    {}

    inline details::TaskPromiseBase &
    GetPromise() const;
};


// Weak reference to task. Does not prevent coroutine from destruction.
class TaskWeakRef: public details::TaskRefBase {
public:
    using TaskRefBase::TaskRefBase;

    /** Obtain reference to task if not yet destroyed. Returns null (empty Task) if already
     * destroyed.
     */
    TaskRef
    Lock();

    /// Make it null.
    void
    Reset()
    {
        cb.Reset();
    }

    /** Locks referenced task, resets this pointer and schedules the task if any. This is typical
     * pattern used in awaiters.
     * @return True if woken, false if task already released.
     */
    bool
    Wakeup()
    {
        TaskRef t = Lock();
        Reset();
        if (t) {
            t.Schedule();
            return true;
        }
        return false;
    }

private:
    friend class TaskRef;
    friend class details::TaskImpl;

    TaskWeakRef(details::TaskCb *cb):
        TaskRefBase(cb)
    {}

    TaskWeakRef(details::TaskCbPtr cb):
        TaskRefBase(etl::move(cb))
    {}
};


namespace details {

/// Task control block associated with every task/awaitable.
struct TaskCb {
    /// Next task when in list, null if last one. Reference should be accounted by a list.
    TaskCb *next = nullptr;
    /// Null when destroyed.
    tasks::CoroutineHandle coro;
    /// Awaiters currently awaiting this task finishing.
    List<details::TaskAwaiterBase *> resultWaiters;
    /// Reference counter for coroutine (strong references). Coroutine is destroyed after last
    /// reference is released if not pinned. Negated values (`-value - 1`) used to indicate that
    /// task is pinned - destruction is prevented when last reference is released.
    etl::atomic<int8_t> coroRefCounter = 1;

    /// Reference counter for control block (weak references).
    etl::atomic<int8_t> refCounter = 1;

    uint8_t priority: tasks::NUM_PRIO_BITS = tasks::LOWEST_PRIORITY,
    /// Task currently queued in runnable queue.
            isRunnable: 1 = 0,
    /// Task finished and result is available.
            isFinished: 1 = 0;

    /// Allocate control block from pool or heap.
    static TaskCb *
    Allocate();

    /// Free control block to pool.
    void
    Free();

    void
    AddRef()
    {
        TaskCbSharedPtrTrait::AddRef(*this);
    }

    bool
    ReleaseRef()
    {
        return TaskCbSharedPtrTrait::ReleaseRef(*this);
    }

    void
    CoroAddRef();

    /// Try adding reference which may not succeed if reference counter already reached zero (e.g.
    /// last reference released concurrently by ISR). This is intended for using by task weak
    /// pointer.
    bool
    CoroTryAddRef();

    /// @return True if last reference released and coroutine was destroyed.
    bool
    CoroReleaseRef();

    TaskRef
    GetRef()
    {
        CoroAddRef();
        return TaskRef(details::TaskCbPtr(this, false));
    }

    void
    Pin();

    /** Unpin task if was previously pinned by `Pin()` method.
     *
     * @return True if task did not have references, was by pinning only, and was destroyed after
     *  unpinning.
     */
    bool
    Unpin();

    /// Wake up all tasks waiting for this task completion.
    void
    NotifyWaiters();

    /// Resume task coroutine.
    void
    Resume();

    /// @return True if added to wait list, false if already finished.
    bool
    AwaitResult(details::TaskAwaiterBase *waiter, const TaskRef &waiterTask);

    void
    CancelAwaitResult(details::TaskAwaiterBase *waiter);

    void
    SetPriority(tasks::Priority priority);

    /// Set new priority if it is higher than current task priority.
    void
    RaisePriority(tasks::Priority priority);
};


using TaskList = List<TaskCb *>;
using TaskTailedList = TailedList<TaskCb *>;


class TaskPromiseBase {
public:
    TaskCbPtr cb;

    TaskPromiseBase();

    ~TaskPromiseBase();

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
        PULSE_PANIC("TaskPromiseBase::unhandled_exception");
    }
};


template <typename TRet>
class TypedTaskPromise: public TaskPromiseBase {
public:
    ~TypedTaskPromise()
    {
        if (cb->isFinished) {
            etl::destroy_at(&GetResult());
        }
    }

    template<etl::convertible_to<TRet> From>
    void
    return_value(From&& from)
    {
        cb->isFinished = 1;
        etl::construct_at<TRet>(reinterpret_cast<TRet *>(result), etl::forward<From>(from));
        cb->NotifyWaiters();
    }

    const TRet &
    GetResult() const
    {
        PULSE_ASSERT(cb->isFinished);
        return *reinterpret_cast<const TRet *>(result);
    }

    /** Move result out of stored value. */
    TRet &&
    MoveResult()
    {
        PULSE_ASSERT(cb->isFinished);
        return etl::move(*reinterpret_cast<TRet *>(result));
    }

private:
    alignas(TRet) uint8_t result[sizeof(TRet)];
};


template <>
class TypedTaskPromise<void>: public TaskPromiseBase {
public:
    void
    return_void()
    {
        cb->isFinished = 1;
        cb->NotifyWaiters();
    }
};


template <typename TRet, bool initialSuspend>
class Task: public TaskRef {
public:
    using TPromise = TaskPromise<TRet, initialSuspend>;

    using TaskRef::TaskRef;

    inline const TRet &
    GetResult() const;

    inline TaskAwaiter<TRet>
    Wait() const;

    inline TaskAwaiter<TRet>
    operator co_await() const;

private:
    TPromise &
    GetPromise() const
    {
        return reinterpret_cast<TPromise &>(TaskRef::GetPromise());
    }
};


template <bool initialSuspend>
class Task<void, initialSuspend>: public TaskRef {
public:
    using TPromise = TaskPromise<void, initialSuspend>;

    using TaskRef::TaskRef;

    inline TaskAwaiter<void>
    Wait() const;

    inline TaskAwaiter<void>
    operator co_await() const;

private:
    TPromise &
    GetPromise() const
    {
        return reinterpret_cast<TPromise &>(TaskRef::GetPromise());
    }
};


/** @tparam initialSuspend Enables initial suspend when true. Tasks spawned by scheduler typically
 * should have it true so that initial body invocation is done when first switched to this task. In
 * contrast, awaitable returned from async function should have it false so that it runs till first
 * suspension point (if any).
 */
template<typename TRet, bool initialSuspend>
class TaskPromise: public TypedTaskPromise<TRet> {
public:
    etl::conditional_t<initialSuspend, std::suspend_always, std::suspend_never>
    initial_suspend()
    {
        return {};
    }

    Task<TRet, initialSuspend>
    get_return_object()
    {
        return Task<TRet, initialSuspend>(TaskPromiseBase::cb);
    }
};


/// Function suitable for spawning tasks.
template <typename F, typename TRet, typename... Args>
concept AsyncTaskFunction = etl::is_invocable_r_v<Task<TRet, true>, F, Args...>;

template <typename T>
struct TaskTraits {
    static constexpr bool isTask = false;
};

template <typename TRet_, bool initialSuspend_>
struct TaskTraits<Task<TRet_, initialSuspend_>> {
    static constexpr bool isTask = true;

    using TRet = TRet_;
    static constexpr bool initialSuspend = initialSuspend_;
};

void
TaskSpawnImpl(const TaskRef &task, tasks::Priority priority = tasks::LOWEST_PRIORITY);

} // namespace details

/// Use it for spawning new tasks by passing it to `tasks::Spawn()`.
template <typename TRet = void>
using Task = details::Task<TRet, true>;

/// Use it as return value for regular async functions.
template <typename TRet = void>
using Awaitable = details::Task<TRet, false>;


/** For now it is mostly marker interface. There is currently no need to make it polymorphic. */
template <typename TRet>
class Awaiter {
public:
    /* Each awaiter should follow some basic rules to make it properly functional and robust.
     *
     * 1. Awaiter should have destructor which should cancel any queued operations. Awaiter may be
     *      created but never awaited, or, more common, it can be used in `Task::WhenAny()` so that
     *      some awaiters are never resumed and are destructed after suspend but before resume. All
     *      these cases should be properly handled by awaiter destructor. Event source should have
     *      API to cancel queued awaiter. This also allows safe destroying of suspended tasks.
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
     *
     * Note, that storing awaiter without awaiting on it across suspension point, may lead to UB if
     * the awaiter event source is already destructed when resumed and awaiting on stored awaiter
     * instance. So, the general rule of thumb is await immediately after creating awaiter or at
     * least on the next suspension point (never after it).
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


namespace details {

/** Abstract base for most common awaiters pattern.
 *
 * @tparam TSourceTrait Should have the following methods:
 *  static void
 *  DequeueAwaiter(TSource *source, AbstractAwaiter *awaiter);
 *
 */
template <typename TRet, class TSource, class TSourceTrait>
class AbstractAwaiter {
public:
    ~AbstractAwaiter();

    bool
    await_ready() const
    {
        return !this->source;
    }

    TRet
    await_resume()
    {
        PULSE_ASSERT(!this->source);
        return etl::move(this->Result());
    }

    /* Typical `await_suspend()` implementation:
     *
     * bool
     * await_suspend(tasks::CoroutineHandle handle)
     * {
     *     auto wTask = TaskRef(handle).GetWeakPtr();
     *
     *     CriticalSection cs;
     *
     *     if (source->HasItem()) [[unlikely]] {
     *         SetResult(etl::move(source->Item()));
     *         source->CommitItemRetrieval();
     *         return false;
     *     }
     *     waiter = etl::move(wTask);
     *     source->QueueAwaiter(this);
     *     return true;
     * }
     */

    /** @return Result, nullopt if not ready. Keep in mind that it can be already moved out by
     * `await_resume()` if the result type supports move construction.
     */
    etl::optional<TRet>
    GetResult() const
    {
        if (!this->HasResult()) {
            return etl::nullopt;
        }
        return Result();
    }

    etl::optional<TRet>
    MoveResult() const
    {
        if (!this->HasResult()) {
            return etl::nullopt;
        }
        return etl::move(Result());
    }

protected:
    friend TSource;
    friend struct details::ListDefaultTrait<AbstractAwaiter *>;

    AbstractAwaiter *next = nullptr;
    TSource *source = nullptr;
    TaskWeakRef waiter;
    alignas(TRet) uint8_t storage[sizeof(TRet)];

    AbstractAwaiter() = delete;

    /** Construct with result to create it in ready state. */
    AbstractAwaiter(TRet &&result)
    {
        etl::construct_at(&this->Result(), etl::move(result));
    }

    AbstractAwaiter(const TRet &result)
    {
        etl::construct_at(&this->Result(), result);
    }

    /** Construct with source to create it in pending state. */
    AbstractAwaiter(TSource *source):
        source(source)
    {}

    TRet &
    Result()
    {
        return *reinterpret_cast<TRet *>(storage);
    }

    bool
    HasResult() const
    {
        return !source;
    }

    template <typename T>
    void
    SetResult(T &&from)
    {
        etl::construct_at(&this->Result(), etl::forward<T>(from));
        source = nullptr;
    }

    void
    SetResult()
    {
        etl::construct_at(&this->Result());
        source = nullptr;
    }

    bool
    Wakeup()
    {
        return waiter.Wakeup();
    }
};

} // namespace details


// Awaiter for explicit task switching.
class TaskSwitchAwaiter: public Awaiter<void> {
public:
    bool
    await_ready() const
    {
        return false;
    }

    bool
    await_suspend(tasks::CoroutineHandle coro);

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
    TaskAwaiterBase(TaskRef task):
        task(etl::move(task))
    {}

    ~TaskAwaiterBase()
    {
        if (waiter) {
            task.cb->CancelAwaitResult(this);
        }
    }

    bool
    await_ready() const
    {
        return task.IsFinished();
    }

    bool
    await_suspend(tasks::CoroutineHandle handle)
    {
        TaskRef waiterTask(handle);
        if (!task.cb->AwaitResult(this, waiterTask)) {
            return false;
        }
        waiter = waiterTask.GetWeakPtr();
        return true;
    }

protected:
    friend struct details::ListDefaultTrait<TaskAwaiterBase *>;
    friend struct TaskCb;

    TaskRef task;
    TaskAwaiterBase *next = nullptr;
    TaskWeakRef waiter;
};

} // namespace details

/**
 * Await task termination and get its result value. Note, that if the task result type is not
 * copy-constructible its result value is moved out of stored value in the task promise object, so
 * that any subsequent result retrieval will get empty (moved out) value.
 * @tparam TRet Task return type.
 */
template <typename TRet>
class TaskAwaiter: public details::TaskAwaiterBase, public Awaiter<TRet> {
public:
    template <bool initialSuspend>
    TaskAwaiter(details::Task<TRet, initialSuspend> task):
        TaskAwaiterBase(etl::move(task))
    {}

    TRet
    await_resume() const
    {
        auto &promise = reinterpret_cast<details::TypedTaskPromise<TRet> &>(task.GetPromise());
        if constexpr (etl::is_copy_constructible_v<TRet>) {
            return promise.GetResult();
        } else {
            return promise.MoveResult();
        }
    }
};


template <>
class TaskAwaiter<void>: public details::TaskAwaiterBase, public Awaiter<void> {
public:
    TaskAwaiter(TaskRef task):
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
        TaskRef target, handler;

        void
        Reset()
        {
            target.ReleaseHandle();
            handler.ReleaseHandle();
        }
    };

    MultipleTasksAwaiterBase() = default;

    TaskWeakRef waiter;
};


template <size_t NumTasks = etl::dynamic_extent>
class MultipleTasksAwaiter: protected MultipleTasksAwaiterBase {
protected:
    etl::array<Entry, NumTasks> tasks;
    static constexpr size_t numTasks = NumTasks;
    bool isFinished = false;

    MultipleTasksAwaiter(const etl::span<TaskRef, NumTasks> &)
    {}
};

template <>
class MultipleTasksAwaiter<etl::dynamic_extent>: protected MultipleTasksAwaiterBase {
protected:
    etl::unique_ptr<Entry[]> tasks;
    const size_t numTasks;

    MultipleTasksAwaiter(const etl::span<TaskRef, etl::dynamic_extent> &tasks):
        tasks(new Entry[tasks.size()]),
        numTasks(tasks.size())
    {}
};

} // namespace details


template <size_t NumTasks>
class AllTasksAwaiter: public details::MultipleTasksAwaiter<NumTasks>, public Awaiter<void> {
public:
    AllTasksAwaiter(const etl::span<TaskRef, NumTasks> &tasks);

    bool
    await_ready() const
    {
        return numLeft == 0;
    }

    bool
    await_suspend(tasks::CoroutineHandle handle);

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
    AnyTaskAwaiter(const etl::span<TaskRef, NumTasks> &tasks);

    bool
    await_ready() const
    {
        return result != NONE;
    }

    bool
    await_suspend(tasks::CoroutineHandle handle);

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

namespace tasks {

#if pulseConfig_SCHEDULED_STATS

struct SchedulerStats {
    /// Number of tasks (task control blocks) currently being referenced.
    uint32_t numActiveTasks,
    /// Number of free task control blocks in the free pool.
             numFreeTasks,
    /// Number of task control blocks dynamically allocated from the heap.
             numDynamicAllocations;
};

void
GetSchedulerStats(SchedulerStats &stats);

#endif // pulseConfig_SCHEDULED_STATS

/** Pass the task to the scheduler. The returned reference should be kept until task is finished
 * otherwise it might be destroyed once the first suspension point is reached (scheduler releases
 * its reference and awaiters are not allowed to store task reference to prevent reference loop).
 */
template <typename TRet>
Task<TRet>
Spawn(Task<TRet> task, Priority priority = LOWEST_PRIORITY)
{
    details::TaskSpawnImpl(task, priority);
    return etl::move(task);
}

/** Spawn new task by providing task function.
    * @return Created task.
    */
template <typename F, typename... Args>
requires details::AsyncTaskFunction<F,
    typename details::TaskTraits<etl::invoke_result_t<F, Args...>>::TRet, Args...>
auto
Spawn(F &&func, Args &&... args)
{
    auto task = etl::invoke(etl::forward<F>(func), etl::forward<Args>(args)...);
    details::TaskSpawnImpl(task, LOWEST_PRIORITY);
    return etl::move(task);
}

/** Spawn new task by providing task function.
    * @return Created task.
    */
template <typename F, typename... Args>
requires details::AsyncTaskFunction<F,
    typename details::TaskTraits<etl::invoke_result_t<F, Args...>>::TRet, Args...>
auto
Spawn(F &&func, Priority priority, Args &&... args)
{
    auto task = etl::invoke(etl::forward<F>(func), etl::forward<Args>(args)...);
    details::TaskSpawnImpl(task, priority);
    return etl::move(task);
}

/** Spawn new task by providing synchronous function. The function is called when task is run by the
 * scheduler. Can be used to defer the call, e.g. from ISR.
 * @return Created task.
 */
template <typename F, typename... Args>
requires etl::is_invocable_v<F, Args...> &&
    (!details::TaskTraits<etl::invoke_result_t<F, Args...>>::isTask)
auto
Spawn(F &&func, Args &&... args)
{
    using TRet = etl::remove_cvref_t<etl::invoke_result_t<F, Args...>>;

    auto taskFunc = [](etl::remove_cvref_t<F> func,
                       etl::remove_cvref_t<Args>... args) -> Task<TRet> {
        co_return etl::invoke(etl::move(func), etl::move(args)...);
    };

    auto task = taskFunc(etl::forward<F>(func), etl::forward<Args>(args)...);
    details::TaskSpawnImpl(task, LOWEST_PRIORITY);
    return etl::move(task);
}

/** Spawn new task by providing synchronous function. The function is called when task is run by the
 * scheduler. Can be used to defer the call, e.g. from ISR.
 * @return Created task.
 */
template <typename F, typename... Args>
requires etl::is_invocable_v<F, Args...> &&
    (!details::TaskTraits<etl::invoke_result_t<F, Args...>>::isTask)
auto
Spawn(F &&func, Priority priority, Args &&... args)
{
    using TRet = etl::remove_cvref_t<etl::invoke_result_t<F, Args...>>;

    auto taskFunc = [](etl::remove_cvref_t<F> func,
                       etl::remove_cvref_t<Args>... args) -> Task<TRet> {
        co_return etl::invoke(etl::move(func), etl::move(args)...);
    };

    auto task = taskFunc(etl::forward<F>(func), etl::forward<Args>(args)...);
    details::TaskSpawnImpl(task, priority);
    return etl::move(task);
}

/**
 * Run main loop of tasks scheduler. Never returns.
 */
void
RunScheduler();

/** Run tasks which are currently in runnable state. Exits when no more runnable task. */
void
RunSome();

/** @return Currently running task if any. This is top-level task, which is run by scheduler. When
* calling asynchronous functions, newly created coroutine is not top-level until it is suspended and
* later resumed by scheduler.
*/
TaskRef
GetCurrentTask();

/** Switch to other runnable task if any. `co_await` returns true if task was switched, false if
 * immediately returned to calling task.
 * @code
 * co_await Task::Switch();
 * @endcode
 */
inline TaskSwitchAwaiter
Switch()
{
    return {};
}

/** Wait when all of the provided tasks complete. Uses dynamic allocation for tasks list if
 * `NumTasks` is `etl::dynamic_extent`.
 */
template<size_t NumTasks = etl::dynamic_extent>
static inline AllTasksAwaiter<NumTasks>
WhenAll(const etl::span<TaskRef, NumTasks> &tasks);

/* Wait when any of the provided tasks completes. `co_await` returns index of the first completed
 * task. Uses dynamic allocation for tasks list if `NumTasks` is `etl::dynamic_extent`.
 */
template<size_t NumTasks = etl::dynamic_extent>
static inline AnyTaskAwaiter<NumTasks>
WhenAny(const etl::span<TaskRef, NumTasks> &tasks);

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

/** Helper to wrap awaiters into task. Can also accept TaskRef or Task<> and return it as is. Passed
 * awaiter lifetime should be at least until the returned task is finished (awaiter resumes).
 */
template <class T>
static inline TaskRef
MakeTask(T &&obj);

/** Helper to save arbitrary awaiter result into the specified variable. Useful when waiting on
 * multiple tasks by `WhenAll` or `WhenAny`. Be very careful with the passed awaiter lifetime - it
 * should not be shorter than the resulting coroutine.
 */
template <typename TResult, typename TAwaiter>
static Awaitable<void>
SaveResult(TAwaiter &&awaiter, etl::optional<TResult> &result);

template <typename TResult, typename TAwaiter>
static Awaitable<void>
SaveResult(TAwaiter &&awaiter, TResult &result);

} // namespace tasks


// /////////////////////////////////////////////////////////////////////////////////////////////////

void
details::TaskCbSharedPtrTrait::Delete(TaskCb &cb)
{
    cb.Free();
}


bool
details::TaskRefBase::IsFinished() const
{
    PULSE_ASSERT(cb);
    return cb->isFinished;
}

tasks::Priority
details::TaskRefBase::GetPriority() const
{
    PULSE_ASSERT(cb);
    return cb->priority;
}


TaskRef::TaskRef(const TaskRef &other):
    TaskRefBase(other)
{
    if (cb) {
        cb->CoroAddRef();
    }
}

TaskRef &
TaskRef::operator =(const TaskRef &other)
{
    ReleaseHandle();
    cb = other.cb;
    if (cb) {
        cb->CoroAddRef();
    }
    return *this;
}

TaskRef &
TaskRef::operator =(TaskRef &&other)
{
    ReleaseHandle();
    cb = etl::move(other.cb);
    return *this;
}

void
TaskRef::SetPriority(tasks::Priority priority) const
{
    cb->SetPriority(priority);
}

void
TaskRef::RaisePriority(tasks::Priority priority) const
{
    cb->RaisePriority(priority);
}

TaskRef &
TaskRef::Pin()
{
    PULSE_ASSERT(cb);
    cb->Pin();
    return *this;
}

TaskWeakRef
TaskRef::GetWeakPtr() const
{
    return TaskWeakRef(cb);
}

details::TaskPromiseBase &
TaskRef::GetPromise() const
{
    PULSE_ASSERT(cb);
    PULSE_ASSERT(cb->coro);
    return cb->coro.promise();
}

TaskAwaiter<void>
TaskRef::Wait() const
{
    return TaskAwaiter<void>(*this);
}

TaskAwaiter<void>
TaskRef::operator co_await() const
{
    return Wait();
}


template <typename TRet, bool initialSuspend>
const TRet &
details::Task<TRet, initialSuspend>::GetResult() const
{
    return GetPromise().GetResult();
}

template <typename TRet, bool initialSuspend>
TaskAwaiter<TRet>
details::Task<TRet, initialSuspend>::Wait() const
{
    return TaskAwaiter<TRet>(*this);
}

template <typename TRet, bool initialSuspend>
TaskAwaiter<TRet>
details::Task<TRet, initialSuspend>::operator co_await() const
{
    return Wait();
}


template <bool initialSuspend>
TaskAwaiter<void>
details::Task<void, initialSuspend>::Wait() const
{
    return TaskAwaiter<void>(*this);
}

template <bool initialSuspend>
TaskAwaiter<void>
details::Task<void, initialSuspend>::operator co_await() const
{
    return Wait();
}


template <typename TRet, class TSource, class TSourceTrait>
details::AbstractAwaiter<TRet, TSource, TSourceTrait>::~AbstractAwaiter()
{
    CriticalSection cs;
    if (source) {
        if (waiter) {
            TSourceTrait::DequeueAwaiter(source, this);
        }
    } else {
        cs.Exit();
        etl::destroy_at(&this->Result());
    }
}


template <size_t NumTasks>
AllTasksAwaiter<NumTasks>::AllTasksAwaiter(const etl::span<TaskRef, NumTasks> &tasks):
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

    for (size_t i = 0; i < this->numTasks; i++) {
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
AllTasksAwaiter<NumTasks>::await_suspend(tasks::CoroutineHandle handle)
{
    if (numLeft == 0) {
        return false;
    }

    TaskRef waiterTask(handle);
    this->waiter = waiterTask.GetWeakPtr();

    // Handlers should have the same priority as waiter. Since waiter is blocked until all tasks are
    // completed, priority is also propagated to all targets.
    tasks::Priority pri = waiterTask.GetPriority();
    for (size_t i = 0; i < this->numTasks; i++) {
        Entry &e = this->tasks[i];
        if (e.handler) {
            e.handler.SetPriority(pri);
            e.target.RaisePriority(pri);
        }
    }

    return true;
}

template <size_t NumTasks>
AnyTaskAwaiter<NumTasks>::AnyTaskAwaiter(const etl::span<TaskRef, NumTasks> &tasks):
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

    for (size_t i = 0; i < this->numTasks; i++) {
        Entry &e = this->tasks[i];
        e.target = tasks[i];
        PULSE_ASSERT(e.target);
        auto h = handler(i, this);
        if (e.target) {
            // If was not instantly released in handler.
            e.handler = etl::move(h);
        } else {
            PULSE_ASSERT(result != NONE);
            for (size_t i = 0; i < this->numTasks; i++) {
                this->tasks[i].Reset();
            }
            break;
        }
    }
}

template <size_t NumTasks>
bool
AnyTaskAwaiter<NumTasks>::await_suspend(tasks::CoroutineHandle handle)
{
    if (result != NONE) {
        return false;
    }

    TaskRef waiterTask(handle);
    this->waiter = waiterTask.GetWeakPtr();

    // Handlers should have the same priority as waiter.
    tasks::Priority pri = waiterTask.GetPriority();
    for (size_t i = 0; i < this->numTasks; i++) {
        Entry &e = this->tasks[i];
        e.handler.SetPriority(pri);
    }

    return true;
}


template<size_t NumTasks>
AllTasksAwaiter<NumTasks>
tasks::WhenAll(const etl::span<TaskRef, NumTasks> &tasks)
{
    return AllTasksAwaiter(tasks);
}

template<size_t NumTasks>
AnyTaskAwaiter<NumTasks>
tasks::WhenAny(const etl::span<TaskRef, NumTasks> &tasks)
{
    return AnyTaskAwaiter(tasks);
}

template <class T1, class T2, class... Args>
AllTasksAwaiter<sizeof...(Args) + 2>
tasks::WhenAll(T1 &&task1, T2 &&task2, Args &&... tasks)
{
    TaskRef _tasks[] = {
        tasks::MakeTask(etl::forward<T1>(task1)),
        tasks::MakeTask(etl::forward<T2>(task2)),
        tasks::MakeTask(etl::forward<Args>(tasks))...
    };
    return WhenAll(etl::span<TaskRef, sizeof...(Args) + 2>(_tasks));
}

template <class T1, class T2, class... Args>
AnyTaskAwaiter<sizeof...(Args) + 2>
tasks::WhenAny(T1 &&task1, T2 &&task2, Args &&... tasks)
{
    TaskRef _tasks[] = {
        tasks::MakeTask(etl::forward<T1>(task1)),
        tasks::MakeTask(etl::forward<T2>(task2)),
        tasks::MakeTask(etl::forward<Args>(tasks))...
    };
    return WhenAny(etl::span<TaskRef, sizeof...(Args) + 2>(_tasks));
}


namespace details {

template <class T>
struct AwaitableWrapper {
    static inline Awaitable<void>
    MakeTask(const T &obj)
    {
        // Passed awaiter lifetime should not end before the task completion, so assuming it is
        // safe. `await_suspend()` requires non-const reference in most cases.
        co_await const_cast<T &>(obj);
    }
};

template <typename T>
concept TaskType = etl::derived_from<T, TaskRef>;

template <TaskType T>
struct AwaitableWrapper<T> {
    template <class U>
    static inline TaskRef
    MakeTask(U &&obj)
    {
        return etl::forward<U>(obj);
    }
};

} // namespace details

template <class T>
TaskRef
tasks::MakeTask(T &&obj)
{
    return details::AwaitableWrapper<etl::remove_cvref_t<T>>::MakeTask(etl::forward<T>(obj));
}


template <typename TResult, typename TAwaiter>
Awaitable<void>
tasks::SaveResult(TAwaiter &&awaiter, etl::optional<TResult> &result)
{
    result.emplace(co_await awaiter);
}

template <typename TResult, typename TAwaiter>
Awaitable<void>
tasks::SaveResult(TAwaiter &&awaiter, TResult &result)
{
    result = co_await awaiter;
}

} // namespace pulse


// Bind TaskPromise to Task coroutine type.
template<typename TRet, bool initialSuspend, typename... Args>
struct std::coroutine_traits<pulse::details::Task<TRet, initialSuspend>, Args...> {
    using promise_type = typename pulse::details::Task<TRet, initialSuspend>::TPromise;
};

#endif /* TASK_H */
