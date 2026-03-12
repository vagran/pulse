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

/** Tasks in runnable state, arranged by priority. */
TaskTailedList readyTasks[pulseConfig_NUM_TASK_PRIORITIES];

/** Each set bit corresponds to non-empty queue for corresponding priority. */
PriorityBitmap readyTasksBitmap;

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

Task &
Task::operator =(const Task &other)
{
    if (handle && GetPromise().ReleaseRef()) {
        handle.destroy();
    }
    handle = other.handle;
    if (handle) {
        GetPromise().AddRef();
    }
    return *this;
}

Task &
Task::operator =(Task &&other) noexcept
{
    if (handle && GetPromise().ReleaseRef()) {
        handle.destroy();
    }
    handle = other.handle;
    other.handle = CoroutineHandle();
    return *this;
}

const Task &
Task::Spawn(Task task, Priority priority)
{
    //XXX
}

TaskPromise::~TaskPromise()
{
    //XXX need to deregister somewhere?
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
TaskList::AddFirst(const Task &task)
{
    if (!head) {
        head = task;
    } else {
        TaskPromise &promise = task.GetPromise();
        promise.next = head;
        head = task;
    }
}

void
TaskTailedList::AddFirst(const Task &task)
{
    if (!head) {
        PULSE_ASSERT(!tail);
        head = task;
        tail = task;
    } else {
        TaskPromise &promise = task.GetPromise();
        promise.next = head;
        head = task;
    }
}

void
TaskTailedList::AddLast(const Task &task)
{
    if (!tail) {
        head = task;
        tail = task;
    } else {
        TaskPromise &promise = task.GetPromise();
        promise.next = task;
        tail = task;
    }
}
