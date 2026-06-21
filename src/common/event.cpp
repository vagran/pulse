#include <pulse/event.h>

using namespace pulse;


Event::~Event()
{
    // Signal and unbind all waiters.
    CriticalSection cs;
    for (auto waiter: waiters) {
        if (!waiter->Wakeup()) {
            continue;
        }
        waiter->SetResult();
    }
}

bool
Event::Set()
{
    CriticalSection cs;

    if (isSet) {
        PULSE_ASSERT(waiters.IsEmpty());
        return true;
    }
    isSet = true;

    while (!waiters.IsEmpty()) {
        auto waiter = waiters.PopFirst();
        if (!waiter->Wakeup()) {
            continue;
        }
        waiter->SetResult();
    }
    return false;
}

bool
Event::Unset()
{
    CriticalSection cs;
    bool ret = isSet;
    isSet = false;
    return ret;
}

EventAwaiter
Event::Wait()
{
    CriticalSection cs;
    if (isSet) {
        return {};
    }
    return EventAwaiter(this);
}


void
Event::AwaiterSourceTrait::DequeueAwaiter(Event *ev, TAwaiterBase *awaiter)
{
    ev->waiters.Remove(awaiter);
}


bool
EventAwaiter::await_suspend(tasks::CoroutineHandle handle)
{
    CriticalSection cs;

    auto ev = this->source;
    if (ev->isSet) {
        cs.Exit();
        this->SetResult();
        return false;
    }

    this->waiter = TaskRef(handle).GetWeakPtr();;
    ev->waiters.AddLast(this);
    return true;
}
