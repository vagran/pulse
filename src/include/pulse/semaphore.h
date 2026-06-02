#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <pulse/task.h>
#include <pulse/port.h>


namespace pulse {

template <class TSem>
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
    SemaphoreAwaiter<Semaphore>
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
    struct AwaiterSourceTrait;
    using TAbstractAwaiter = details::AbstractAwaiter<bool, Semaphore, AwaiterSourceTrait>;

    friend class SemaphoreAwaiter<Semaphore>;

    TailedList<TAbstractAwaiter *> waiters;
    const TSize numTokens;
    TSize numAcquired;

    struct AwaiterSourceTrait {
        static void
        DequeueAwaiter(Semaphore *sem, TAbstractAwaiter *awaiter)
        {
            sem->waiters.Remove(awaiter);
        }
    };
};


template <class TSem>
class SemaphoreAwaiter: public TSem::TAbstractAwaiter {
public:
    bool
    await_suspend(tasks::CoroutineHandle handle);

private:
    friend TSem;
    using Base = TSem::TAbstractAwaiter;

    using Base::Base;
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
    CriticalSection cs;
    for (auto waiter: waiters) {
        if (!waiter->Wakeup()) {
            continue;
        }
        // Semaphore destroyed, token not acquired.
        waiter->SetResult(false);
    }
}

template <etl::unsigned_integral TSize>
SemaphoreAwaiter<Semaphore<TSize>>
Semaphore<TSize>::Acquire()
{
    CriticalSection cs;

    if (numAcquired < numTokens) {
        numAcquired++;
        cs.Exit();
        return SemaphoreAwaiter<Semaphore<TSize>>(true);
    }
    cs.Exit();
    return SemaphoreAwaiter<Semaphore<TSize>>(this);
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
        if (!waiter->Wakeup()) {
            continue;
        }
        waiter->SetResult(true);
        numAcquired++;
    }
}


template <class TSem>
bool
SemaphoreAwaiter<TSem>::await_suspend(tasks::CoroutineHandle handle)
{
    auto wTask = TaskRef(handle).GetWeakPtr();

    CriticalSection cs;

    auto sem = this->source;
    if (sem->numAcquired < sem->numTokens) {
        sem->numAcquired++;
        cs.Exit();
        this->SetResult(true);
        return false;
    }

    this->waiter = etl::move(wTask);
    sem->waiters.AddLast(this);
    return true;
}

} // namespace pulse

#endif /* SEMAPHORE_H */
