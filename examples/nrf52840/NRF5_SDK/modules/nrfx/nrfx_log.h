#ifndef NRFX_LOG_H
#define NRFX_LOG_H

#ifndef NRFX_LOG_ERROR
#define NRFX_LOG_ERROR(...)
#endif
#ifndef NRFX_LOG_WARNING
#define NRFX_LOG_WARNING(...)
#endif
#ifndef NRFX_LOG_INFO
#define NRFX_LOG_INFO(...)
#endif
#ifndef NRFX_LOG_DEBUG
#define NRFX_LOG_DEBUG(...)
#endif

#ifndef NRFX_LOG_HEXDUMP_ERROR
#define NRFX_LOG_HEXDUMP_ERROR(p_memory, length)
#endif
#ifndef NRFX_LOG_HEXDUMP_WARNING
#define NRFX_LOG_HEXDUMP_WARNING(p_memory, length)
#endif
#ifndef NRFX_LOG_HEXDUMP_INFO
#define NRFX_LOG_HEXDUMP_INFO(p_memory, length)
#endif
#ifndef NRFX_LOG_HEXDUMP_DEBUG
#define NRFX_LOG_HEXDUMP_DEBUG(p_memory, length)
#endif

#ifndef NRFX_LOG_ERROR_STRING_GET
#define NRFX_LOG_ERROR_STRING_GET(error_code)
#endif

#endif /* NRFX_LOG_H */
