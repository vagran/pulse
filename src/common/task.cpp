#include <pulse/task.h>
#include <etl/limits.h>
#include <etl/utility.h>
#include <etl/bitset.h>


using namespace pulse;


namespace {

using PriorityBitmap = SizedUint<pulseConfig_NUM_TASK_PRIORITIES>;

static_assert(pulseConfig_NUM_TASK_PRIORITIES >= 2,
              "pulseConfig_NUM_TASK_PRIORITIES must be at least 2");

static_assert(pulseConfig_NUM_TASK_PRIORITIES < sizeof(PriorityBitmap) * 8,
              "pulseConfig_NUM_TASK_PRIORITIES too big");

static_assert(pulseConfig_MAX_TASKS > 0, "pulseConfig_MAX_TASKS should be positive");

class TaskSlot {
public:
    union {
        Task task;
        /** Next free slot when in free list. */
        TaskSlot *next;
    };

    TaskSlot():
        next(nullptr)
    {}

    ~TaskSlot()
    {}

    static inline
    Task::Id
    GetId(const TaskSlot *slot);

    inline Task::Id
    GetId() const;

    void
    Create(Task &&task)
    {
        new(&this->task) Task(etl::move(task));
    }

    void
    Destroy()
    {
        task.~Task();
    }
};

TaskSlot tasks[pulseConfig_MAX_TASKS];

/// Set bit corresponds to allocated task.
etl::bitset<pulseConfig_MAX_TASKS> tasksBitmap;

/** Points to the first task in free list. */
struct FreeList {
    Task::Id head;

    FreeList()
    {
        // ID is one-based index in tasks array.
        head = 1;
        for (Task::Id idx = 0; idx < pulseConfig_MAX_TASKS; idx++) {
            tasks[idx].next = idx < pulseConfig_MAX_TASKS - 1 ? &tasks[idx + 1] : nullptr;
        }
    }

    inline bool
    IsEmpty() const
    {
        return head == Task::ID_NONE;
    }
} freeTasks;

/** Tasks in runnable state, arranged by priority. */
TaskTailedList readyTasks[pulseConfig_NUM_TASK_PRIORITIES];

/** Each set bit corresponds to non-empty queue for corresponding priority. */
PriorityBitmap readyTasksBitmap;


TaskSlot *
TaskSlotById(Task::Id id)
{
    if (id == Task::ID_NONE) {
        return nullptr;
    }
    return &tasks[id - 1];
}

Task &
TaskById(Task::Id id)
{
    PULSE_ASSERT(id != Task::ID_NONE);
    Task &task = tasks[id - 1].task;
    PULSE_ASSERT(task.GetPromise().id == id);
    return task;
}

void
FreeTask(Task::Id id)
{
    TaskSlot *slot = TaskSlotById(id);
    slot->Destroy();
    slot->next = TaskSlotById(freeTasks.head);
    freeTasks.head = slot->GetId();
    tasksBitmap.set(id, false);
}

Task::Id
AllocateTask(Task &&task)
{
    if (freeTasks.IsEmpty()) {
                // No free slot
#if pulseConfig_PANIC_ON_TASK_SPAWN_FAILURE
        PULSE_PANIC("No free task");
#endif
        return Task::ID_NONE;
    }
    Task::Id id = freeTasks.head;
    TaskSlot *slot = TaskSlotById(id);
    freeTasks.head = TaskSlot::GetId(slot->next);
    tasksBitmap.set(id - 1);
    slot->Create(etl::move(task));
    return id;
}

inline Task::Id
TaskSlot::GetId(const TaskSlot *slot)
{
    return slot - tasks + 1;
}

inline Task::Id
TaskSlot::GetId() const
{
    return GetId(this);
}

} // anonymous namespace

Task::TTask(CoroutineHandle handle):
    handle(handle)
{
    GetPromise().AddRef();
}

Task::TTask(const Task &other):
    handle(other.handle)
{
    if (handle) {
        GetPromise().AddRef();
    }
}

Task::~TTask()
{
    if (handle && GetPromise().ReleaseRef()) {
        handle.destroy();
    }
}

Task::Id
Task::GetId() const
{
    return GetPromise().id;
}

Task
Task::ById(Id id)
{
    if (id == ID_NONE || !tasksBitmap[id - 1]) {
        return Task();
    }
    return TaskById(id);
}

Task::Id
Task::Spawn(Task task, Priority priority)
{

    //XXX
    return AllocateTask(etl::move(task));
}

TaskPromise::~TaskPromise()
{
    if (id != Task::ID_NONE) {
        //XXX remove from scheduler
    }
}

void
TaskPromise::AddRef()
{
    if (refCounter == etl::numeric_limits<decltype(refCounter)>::max()) {
        PULSE_PANIC("Task reference counter overflow");
    }
    refCounter++;
}

void
TaskList::AddFirst(Task::Id taskId)
{
    PULSE_ASSERT(Task::ById(taskId).GetPromise().next == Task::ID_NONE);
    if (head == Task::ID_NONE) {
        head = taskId;
    } else {
        TaskPromise &promise = Task::ById(taskId).GetPromise();
        promise.next = head;
        head = taskId;
    }
}

void
TaskTailedList::AddFirst(Task::Id taskId)
{
    PULSE_ASSERT(Task::ById(taskId).GetPromise().next == Task::ID_NONE);
    if (head == Task::ID_NONE) {
        PULSE_ASSERT(tail == Task::ID_NONE);
        head = taskId;
        tail = taskId;
    } else {
        TaskPromise &promise = Task::ById(taskId).GetPromise();
        promise.next = head;
        head = taskId;
    }
}

void
TaskTailedList::AddLast(Task::Id taskId)
{
    PULSE_ASSERT(Task::ById(taskId).GetPromise().next == Task::ID_NONE);
    if (tail == Task::ID_NONE) {
        PULSE_ASSERT(head == Task::ID_NONE);
        head = taskId;
        tail = taskId;
    } else {
        TaskPromise &promise = Task::ById(tail).GetPromise();
        promise.next = taskId;
        tail = taskId;
    }
}
