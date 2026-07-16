#ifndef PULSE_DEBUG_H
#define PULSE_DEBUG_H

#include <pulse/config.h>

#define PULSE_ASSERT(x)     pulseConfig_ASSERT(x)

#define PULSE_PANIC(msg)    pulseConfig_PANIC(msg)

#endif /* PULSE_DEBUG_H */
