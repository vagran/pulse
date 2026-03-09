#include <pulse/task.h>
#include <etl/limits.h>
#include <etl/utility.h>


using namespace pulse;


namespace {

using PriorityBitmap = uint32_t;

static_assert(pulseConfig_NUM_TASK_PRIORITIES < sizeof(PriorityBitmap) * 8,
              "pulseConfig_NUM_TASK_PRIORITIES too big");

// Single element tasks list is indistinguishable from uninitialized list so enforce this.
static_assert(pulseConfig_MAX_TASKS >= 2, "pulseConfig_MAX_TASKS should be at least 2");

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
/** Points to the first task in free list. */
Task::Id freeTasks = Task::ID_NONE;

/** Tasks in runnable state, arranged by priority. */
Task::Id readyTasks[pulseConfig_NUM_TASK_PRIORITIES];
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

void
FreeTask(Task::Id id)
{
    TaskSlot *slot = TaskSlotById(id);
    slot->Destroy();
    slot->next = TaskSlotById(freeTasks);
    freeTasks = slot->GetId();
}

Task::Id
AllocateTask(Task &&task)
{
    if (freeTasks == Task::ID_NONE) {
        return Task::ID_NONE;
    }
    TaskSlot *slot = TaskSlotById(freeTasks);
    freeTasks = TaskSlot::GetId(slot->next);
    slot->Create(etl::move(task));
    return slot->GetId();
}

void
InitializeTasks()
{
    // ID is one-based index in tasks array.
    freeTasks = 1;
    for (Task::Id idx = 0; idx < pulseConfig_MAX_TASKS; idx++) {
        tasks[idx].next = idx < pulseConfig_MAX_TASKS - 1 ? &tasks[idx + 1] : nullptr;
    }
}

} // anonymous namespace

Task::Id
Task::Spawn(Task &&task, Priority priority)
{
    if (freeTasks == Task::ID_NONE) {
        if (tasks[0].next == nullptr) {
            // Not yet initialized
            InitializeTasks();
        } else {
            // No free slot
#if pulseConfig_PANIC_ON_TASK_SPAWN_FAILURE
            PULSE_PANIC("No free task");
#endif
            return Task::ID_NONE;
        }
    }

    //XXX
    return AllocateTask(etl::move(task));
}
