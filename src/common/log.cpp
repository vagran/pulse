#include <pulse/log.h>

using namespace pulse::log;

#if PULSE_LOG_ENABLED

details::LogOutputStream details::output;

void
details::WriteLogPrefix(Level level)
{
    //XXX support timestamp

    switch (level) {
    case Level::DEBUG:
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
