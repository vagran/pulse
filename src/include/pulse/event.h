#ifndef EVENT_H
#define EVENT_H

#include <pulse/task.h>


namespace pulse {

class EventAwaiter;


/// Single flag which can be set or reset, and awaited for being set.
class Event {
public:
    Event(bool isSet = false):
        isSet(isSet)
    {}

    Event(const Event &) = delete;

    ~Event();

    /// Set signalled state.
    /// @return Previous state.
    bool
    Set();

    /// Unset signalled state.
    /// @return Previous state.
    bool
    Unset();

    EventAwaiter
    Wait();

    inline EventAwaiter
    operator co_await();

private:
    struct AwaiterSourceTrait;
    using TAwaiterBase = details::AwaiterBase<void, Event, AwaiterSourceTrait>;

    friend class EventAwaiter;

    TailedList<TAwaiterBase *> waiters;
    bool isSet;

    struct AwaiterSourceTrait {
        static void
        DequeueAwaiter(Event *ev, TAwaiterBase *awaiter);
    };
};


class EventAwaiter: public Event::TAwaiterBase {
public:
    bool
    await_suspend(tasks::CoroutineHandle handle);

private:
    friend Event;
    using Base = Event::TAwaiterBase;

    using Base::Base;
};


EventAwaiter
Event::operator co_await()
{
    return Wait();
}

} // namespace pulse

#endif /* EVENT_H */
