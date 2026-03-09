#ifndef COMMON_H
#define COMMON_H

#include <pulse/details/default_config.h>
#include <etl/type_traits.h>
#include <etl/limits.h>


#define PULSE_ASSERT(x)     pulseConfig_ASSERT(x)

#define PULSE_PANIC(msg)    pulseConfig_PANIC(msg)

namespace pulse {

// Best matched uint type of given size in bits.
template<size_t Bits>
using SizedUint = etl::conditional_t<
    Bits <= 8,  uint8_t,
    etl::conditional_t<Bits <= 16, uint16_t,
        etl::conditional_t<Bits <= 32, uint32_t,
            uint64_t>>>;


template <typename T>
constexpr int
FitUintBits(T value)
{
    if (value < etl::numeric_limits<uint8_t>::max()) {
        return sizeof(uint8_t) * 8;
    } else if (value < etl::numeric_limits<uint16_t>::max()) {
        return sizeof(uint16_t) * 8;
    } else if (value < etl::numeric_limits<uint32_t>::max()) {
        return sizeof(uint32_t) * 8;
    } else {
        return sizeof(uint64_t) * 8;
    }
}

} // namespace pulse

#endif /* COMMON_H */
