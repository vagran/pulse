#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>


namespace pulse {

class Timer {
public:

    using TickCount = uint32_t;

    static constexpr bool
    IsBefore(TickCount t1, TickCount t2)
    {
        return static_cast<int32_t>(t1 - t2) < 0;
    }

    static constexpr bool
    IsAfter(TickCount t1, TickCount t2)
    {
        return static_cast<int32_t>(t1 - t2) > 0;
    }

    
};

} // namespace pulse

#endif /* TIMER_H */
