#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <pulse/task.h>
#include <pulse/port.h>


namespace pulse {

template <etl::unsigned_integral TSize>
class SemaphoreAwaiter;

template <etl::unsigned_integral TSize>
class SemaphoreGuard;


/** Synchronizes access to a limited resource. Semaphore object is constructed with a limited
 * number of available tokens. Each resource user can acquire a token, which later should be
 * released back.
 */
template <etl::unsigned_integral TSize = uint8_t>
class Semaphore {
public:
    Semaphore(const Semaphore &) = delete;

    Semaphore(TSize numTokens, TSize numInitiallyAcquired = 0):
        numTokens(numTokens),
        numAcquired(numInitiallyAcquired)
    {
        PULSE_ASSERT(numTokens > 0);
        PULSE_ASSERT(numInitiallyAcquired <= numTokens);
    }

    ~Semaphore();

    /** Acquire one token from the semaphore. */
    SemaphoreAwaiter<TSize>
    Acquire();

    /** Acquire one token from the semaphore.
     * @return True if acquired, false if no tokens available.
     */
    bool
    TryAcquire();

    Awaitable<SemaphoreGuard<TSize>>
    AcquireGuard();

    /** Release token previously acquired by Acquire(). Can be called from ISR. */
    void
    Release();

private:
    friend class SemaphoreAwaiter<TSize>;

    TailedList<SemaphoreAwaiter<TSize> *> waiters;
    const TSize numTokens;
    TSize numAcquired;
};


template <etl::unsigned_integral TSize>
class SemaphoreAwaiter: public Awaiter<bool> {
public:
    ~SemaphoreAwaiter();

    bool
    await_ready()
    {
        return sem == nullptr;
    }

    bool
    await_suspend(Task::CoroutineHandle handle);

    /** @return True if acquired, false if semaphore destroyed. */
    bool
    await_resume() const
    {
        return isAcquired;
    }

private:
    friend class Semaphore<TSize>;
    friend struct details::ListDefaultTrait<SemaphoreAwaiter<TSize> *>;

    SemaphoreAwaiter<TSize> *next = nullptr;
    Semaphore<TSize> *sem = nullptr;
    Task::WeakPtr task;
    bool isAcquired;

    SemaphoreAwaiter():
        isAcquired(true)
    {}

    SemaphoreAwaiter(Semaphore<TSize> *sem):
        sem(sem),
        isAcquired(false)
    {}
};


/// Uses RAII to hold semaphore token. Guard should not outlive the associated semaphore instance.
template <etl::unsigned_integral TSize>
class SemaphoreGuard {
public:
    SemaphoreGuard(const SemaphoreGuard &) = delete;

    SemaphoreGuard(SemaphoreGuard &&other):
        sem(other.sem)
    {
        other.sem = nullptr;
    }

    ~SemaphoreGuard()
    {
        if (sem) {
            sem->Release();
        }
    }

    operator bool() const
    {
        return sem;
    }

    void
    Release()
    {
        if (sem) {
            sem->Release();
            sem = nullptr;
        }
    }

private:
    friend class Semaphore<TSize>;

    Semaphore<TSize> *sem;

    SemaphoreGuard(Semaphore<TSize> *sem):
        sem(sem)
    {}

    SemaphoreGuard():
        sem(nullptr)
    {}
};


template <etl::unsigned_integral TSize>
Semaphore<TSize>::~Semaphore()
{
    for (auto waiter: waiters) {
        waiter->sem = nullptr;
        waiter->task.Wakeup();
    }
}

template <etl::unsigned_integral TSize>
SemaphoreAwaiter<TSize>
Semaphore<TSize>::Acquire()
{
    CriticalSection cs;

    if (numAcquired < numTokens) {
        numAcquired++;
        cs.Exit();
        return {};
    }
    cs.Exit();
    return SemaphoreAwaiter<TSize>(this);
}

template <etl::unsigned_integral TSize>
bool
Semaphore<TSize>::TryAcquire()
{
    CriticalSection cs;

    if (numAcquired < numTokens) {
        numAcquired++;
        return true;
    }
    return false;
}

template <etl::unsigned_integral TSize>
Awaitable<SemaphoreGuard<TSize>>
Semaphore<TSize>::AcquireGuard()
{
    if (co_await Acquire()) {
        co_return SemaphoreGuard<TSize>(this);
    }
    co_return SemaphoreGuard<TSize>();
}

template <etl::unsigned_integral TSize>
void
Semaphore<TSize>::Release()
{
    CriticalSection cs;

    PULSE_ASSERT(numAcquired);
    numAcquired--;

    while (!waiters.IsEmpty() && numAcquired < numTokens) {
        auto waiter = waiters.PopFirst();
        waiter->sem = nullptr;
        waiter->isAcquired = true;
        waiter->task.Wakeup();
    }
}


template <etl::unsigned_integral TSize>
SemaphoreAwaiter<TSize>::~SemaphoreAwaiter()
{
    if (sem && task) {
        sem->waiters.Remove(this);
    }
}

template <etl::unsigned_integral TSize>
bool
SemaphoreAwaiter<TSize>::await_suspend(Task::CoroutineHandle handle)
{
    // Move possible dynamic allocation out of lock.
    auto wTask = Task(handle).GetWeakPtr();

    CriticalSection cs;

    if (sem->numAcquired < sem->numTokens) {
        sem->numAcquired++;
        cs.Exit();
        isAcquired = true;
        return false;
    }

    task = etl::move(wTask);
    sem->waiters.AddLast(this);
    return true;
}

} // namespace pulse

#endif /* SEMAPHORE_H */
