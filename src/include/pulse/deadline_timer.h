#ifndef DEADLINE_TIMER_H
#define DEADLINE_TIMER_H

#include <pulse/timer.h>


namespace pulse {

class DeadlineTimer {
public:

    DeadlineTimer(Timer::Duration duration):
        until(Timer::GetTime() + duration.duration)
    {}

    Timer::Duration
    Remaining() const
    {
        Timer::TickCount now = Timer::GetTime();
        if (Timer::IsReached(now, until)) {
            return 0;
        }
        return until - now;
    }

    bool
    IsExpired() const
    {
        return Remaining().duration == 0;
    }

private:
    Timer::TickCount until;
};

} // namespace pulse

#endif /* DEADLINE_TIMER_H */
