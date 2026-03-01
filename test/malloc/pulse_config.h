#ifndef PULSE_CONFIG_H
#define PULSE_CONFIG_H

#include <pulse/defs.h>

#define pulseConfig_MALLOC_GRANULARITY              M_GRAN
#define pulseConfig_MALLOC_BLOCK_SIZE_WORD_SIZE     M_BSZ
#define pulseConfig_MALLOC_ALIGNMENT                M_ALIGN
#define pulseConfig_MALLOC_BEST_FIT                 M_FIT

#include <common_pulse_config.h>

#endif /* PULSE_CONFIG_H */
