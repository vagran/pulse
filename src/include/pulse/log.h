#ifndef LOG_H
#define LOG_H

#include <pulse/format.h>


#define PULSE_LOG_LEVEL_DISABLED    0
#define PULSE_LOG_LEVEL_ERROR       1
#define PULSE_LOG_LEVEL_WARNING     2
#define PULSE_LOG_LEVEL_INFO        3
#define PULSE_LOG_LEVEL_DEBUG       4


#if defined(pulseConfig_LOG_PUT_CHAR) && pulseConfig_LOG_LEVEL > PULSE_LOG_LEVEL_DISABLED
#define PULSE_LOG_ENABLED 1
#else
#define PULSE_LOG_ENABLED 0
#endif


namespace pulse {

namespace log {

enum class Level {
    DISABLED = PULSE_LOG_LEVEL_DISABLED,
    ERROR = PULSE_LOG_LEVEL_ERROR,
    WARNING = PULSE_LOG_LEVEL_WARNING,
    INFO = PULSE_LOG_LEVEL_INFO,
    // Prevent conflict with DEBUG macro typically defined in debug builds
    DEBUG_ = PULSE_LOG_LEVEL_DEBUG
};


constexpr Level THRESHOLD_LEVEL = static_cast<Level>(pulseConfig_LOG_LEVEL);

constexpr bool
IsLevelEnabled(Level level)
{
    return level <= THRESHOLD_LEVEL;
}

#if PULSE_LOG_ENABLED

namespace details {

class LogOutputStream: public fmt::OutputStream {
public:
    virtual void
    WriteChar(char c) override
    {
        pulseConfig_LOG_PUT_CHAR(c);
    }
};

extern LogOutputStream output;

void
WriteLogPrefix(Level level);

} // namespace details
#endif

template <class... Args>
void
Write(Level level, etl::string_view fmt, Args&&... args)
{
#if PULSE_LOG_ENABLED
    if (!IsLevelEnabled(level)) {
        return;
    }
    details::WriteLogPrefix(level);
    fmt::FormatTo(details::output, fmt, etl::forward<Args>(args)...);
    details::output.WriteChar('\n');
#endif
}

} // namespace log

} // namespace pulse

#if PULSE_LOG_ENABLED && pulseConfig_LOG_LEVEL >= PULSE_LOG_LEVEL_DEBUG
#define LOG_DEBUG(fmt, ...) pulse::log::Write(pulse::log::Level::DEBUG_, fmt, ## __VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)
#endif

#if PULSE_LOG_ENABLED && pulseConfig_LOG_LEVEL >= PULSE_LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...) pulse::log::Write(pulse::log::Level::INFO, fmt, ## __VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)
#endif

#if PULSE_LOG_ENABLED && pulseConfig_LOG_LEVEL >= PULSE_LOG_LEVEL_WARNING
#define LOG_WARNING(fmt, ...) pulse::log::Write(pulse::log::Level::WARNING, fmt, ## __VA_ARGS__)
#else
#define LOG_WARNING(fmt, ...)
#endif

#if PULSE_LOG_ENABLED && pulseConfig_LOG_LEVEL >= PULSE_LOG_LEVEL_ERROR
#define LOG_ERROR(fmt, ...) pulse::log::Write(pulse::log::Level::ERROR, fmt, ## __VA_ARGS__)
#else
#define LOG_ERROR(fmt, ...)
#endif

#endif /* LOG_H */
