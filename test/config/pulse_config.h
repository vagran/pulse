#ifndef PULSE_CONFIG_H
#define PULSE_CONFIG_H

#include <pulse/defs.h>

#define pulseConfig_MALLOC_GRANULARITY              8
#define pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE     2

extern "C" void FormatError(const char *msg);

#define pulseConfig_FORMAT_ERROR(msg)               FormatError(msg)

#include <common_pulse_config.h>

#endif /* PULSE_CONFIG_H */
