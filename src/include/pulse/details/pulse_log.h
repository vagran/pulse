#ifndef PULSE_LOG_H
#define PULSE_LOG_H

#include <pulse/log.h>

#if defined(pulseConfig_LOG_PUT_CHAR) && pulseConfig_PULSE_LOG_LEVEL > PULSE_LOG_LEVEL_DISABLED
#define PULSE_OWN_LOG_ENABLED 1
#else
#define PULSE_OWN_LOG_ENABLED 0
#endif

#if PULSE_OWN_LOG_ENABLED

#define PULSE_LOG_DEBUG(fmt, ...)       LOG_DEBUG(fmt, ## __VA_ARGS__)
#define PULSE_LOG_INFO(fmt, ...)        LOG_INFO(fmt, ## __VA_ARGS__)
#define PULSE_LOG_WARNING(fmt, ...)     LOG_WARNING(fmt, ## __VA_ARGS__)
#define PULSE_LOG_ERROR(fmt, ...)       LOG_ERROR(fmt, ## __VA_ARGS__)

#else // PULSE_OWN_LOG_ENABLED

#define PULSE_LOG_DEBUG(fmt, ...)
#define PULSE_LOG_INFO(fmt, ...)
#define PULSE_LOG_WARNING(fmt, ...)
#define PULSE_LOG_ERROR(fmt, ...)

#endif // PULSE_OWN_LOG_ENABLED

#endif /* PULSE_LOG_H */
