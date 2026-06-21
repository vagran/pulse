#include <pulse/waitset.h>

using namespace pulse;


WaitsetAwaiter
details::WaitsetBase::WaitAny()
{
    return WaitsetAwaiter(this);
}

void
details::WaitsetBase::Wakeup(TAwaiterBase *waiter, size_t result)
{
    if (waiter->Wakeup()) {
        waiter->SetResult(result);
    }
}

void
details::WaitsetBase::AwaiterSourceTrait::DequeueAwaiter(WaitsetBase *ws, TAwaiterBase *awaiter)
{
    ws->waiters.Remove(awaiter);
}


bool
WaitsetAwaiter::await_suspend(tasks::CoroutineHandle handle)
{
    // Check if something left from previous round, take them first for more uniform consumption.
    if (source->readyMask) {
        this->SetResult(etl::countr_zero(source->readyMask));
        return false;
    }
    source->PrepareWait();
    if (source->readyMask) {
        this->SetResult(etl::countr_zero(source->readyMask));
        return false;
    }
    this->waiter = TaskRef(handle).GetWeakPtr();
    source->waiters.AddLast(this);
    return true;
}
