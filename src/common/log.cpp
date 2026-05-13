#include <pulse/log.h>

using namespace pulse::log;

#if PULSE_LOG_ENABLED

details::LogOutputStream details::output;

void
details::WriteLogPrefix(Level level)
{
#ifdef pulseConfig_LOG_GET_TIMESTAMP
    char buffer[64];
    size_t size = pulseConfig_LOG_GET_TIMESTAMP(buffer, sizeof(buffer));
    output.Write(etl::string_view(buffer, size));
    output.WriteChar(' ');
#endif

    switch (level) {
    case Level::DEBUG_:
        output.WriteChar('D');
        break;
    case Level::INFO:
        output.WriteChar('I');
        break;
    case Level::WARNING:
        output.WriteChar('W');
        break;
    case Level::ERROR:
        output.WriteChar('E');
        break;
    default:
        return;
    }
    output.WriteChar(' ');
}

#endif // PULSE_LOG_ENABLED
