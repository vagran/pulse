/*
 * Common preprocessor definitions.
 */

#ifndef PULSE_DEFS_H_
#define PULSE_DEFS_H_

#define PULSE_CONCAT2(__x, __y)         __x##__y

/** Macro for concatenating identifiers. */
#define PULSE_CONCAT(__x, __y)          PULSE_CONCAT2(__x, __y)

#define PULSE_STR2(__x)                 # __x
/** Macro for stringifying identifiers. */
#define PULSE_STR(__x)                  PULSE_STR2(__x)

#define PULSE_SIZEOF_ARRAY(__array)     (sizeof(__array) / sizeof((__array)[0]))


#define PULSE_PACKED                    __attribute__((packed))

#define PULSE_UNUSED                    __attribute__((unused))

/** Align integer value to the next alignment position. Alignment must be power of 2. */
#define PULSE_ALIGN2(__x, __alignment)  (((__x) + (__alignment) - 1) & ~((__alignment) - 1))

/** Align integer value to the previous alignment position. Alignment must be power of 2. */
#define PULSE_ALIGN2_DOWN(__x, __alignment) ((__x) & ~((__alignment) - 1))

/** Check if value is aligned on the specified alignment value which must be power of 2. */
#define PULSE_IS_ALIGNED2(__x, __alignment) ((__x & (__alignment - 1)) == 0)

/** Check if non-null integer value is power of two. */
#define PULSE_IS_POW2(__x)              ((((__x) - 1) & (__x)) == 0)

#endif /* PULSE_DEFS_H_ */
