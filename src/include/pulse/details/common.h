#ifndef PULSE_DETAILS_COMMON_H
#define PULSE_DETAILS_COMMON_H

#include <pulse/config.h>
#include <etl/type_traits.h>
#include <etl/limits.h>
#include <etl/concepts.h>
#include <etl/bit.h>


#define PULSE_ASSERT(x)     pulseConfig_ASSERT(x)

#define PULSE_PANIC(msg)    pulseConfig_PANIC(msg)

namespace pulse {

// Best matched uint type of given size in bits.
template<size_t Bits>
using SizedUint =
    etl::conditional_t<Bits <= 8, uint8_t,
        etl::conditional_t<Bits <= 16, uint16_t,
            etl::conditional_t<Bits <= 32, uint32_t,
                etl::conditional_t<Bits <= 64, uint64_t, void>>>>;


/// @return Size in bits of smallest unsigned integral type which can store the specified integral
///     value. -1 if out of range.
template <etl::integral T>
consteval int
UintBitWidth(T value)
{
    if (value <= etl::numeric_limits<uint8_t>::max()) {
        return sizeof(uint8_t) * 8;
    } else if (value <= etl::numeric_limits<uint16_t>::max()) {
        return sizeof(uint16_t) * 8;
    } else if (value <= etl::numeric_limits<uint32_t>::max()) {
        return sizeof(uint32_t) * 8;
    } else if (value <= etl::numeric_limits<uint64_t>::max()) {
        return sizeof(uint64_t) * 8;
    } else {
        return -1;
    }
}

/// Same as etl::bit_width but deals with signed types (disallowing negative values, returning -1
/// for them). This is handy for calling directly with integer literals.
template <etl::integral T>
consteval int
BitWidth(T value)
{
    if constexpr (etl::signed_integral<T>) {
        if (value < 0) {
            return -1;
        }
    }
    using U = etl::make_unsigned_t<T>;
    return etl::bit_width(static_cast<U>(value));
}

} // namespace pulse

#endif /* PULSE_DETAILS_COMMON_H */
