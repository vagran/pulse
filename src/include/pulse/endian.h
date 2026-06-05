#ifndef ENDIAN_H
#define ENDIAN_H

#include <pulse/defs.h>
#include <etl/concepts.h>
#include <etl/bit.h>


namespace pulse {

/// Check if the system is little-endian.
constexpr bool
IsSystemLe()
{
    return etl::endian::native == etl::endian::little;
}

/// Check if the system is big-endian.
constexpr bool
IsSystemBe()
{
    return !IsSystemLe();
}


/// Swap bytes in 16-bits integer value.
#define PULSE_BSWAP16(__x)    __builtin_bswap16(__x)
/// Swap bytes in 32-bits integer value.
#define PULSE_BSWAP32(__x)    __builtin_bswap32(__x)
/// Swap bytes in 64-bits integer value.
#define PULSE_BSWAP64(__x)    __builtin_bswap64(__x)


template <typename T>
concept ByteSwappable = etl::integral<T> || etl::floating_point<T>;

template <typename T>
concept ConstexprByteSwappable = etl::integral<T>;

template <typename T>
concept ByteSwappable8 = ByteSwappable<T> && (sizeof(T) == 1);

template <typename T>
concept ByteSwappable16 = ByteSwappable<T> && (sizeof(T) == 2);

template <typename T>
concept ByteSwappable32 = ByteSwappable<T> && (sizeof(T) == 4);

template <typename T>
concept ByteSwappable64 = ByteSwappable<T> && (sizeof(T) == 8);


namespace details {

template <ByteSwappable T>
struct ByteSwapper {
    static constexpr T
    Swap(T x)
    {
        return x;
    }
};

template <ByteSwappable16 T>
struct ByteSwapper<T> {
    static constexpr T
    Swap(T x)
    {
        return PULSE_BSWAP16(x);
    }
};

template <ByteSwappable32 T>
struct ByteSwapper<T> {
    static constexpr T
    Swap(T x)
    {
        return PULSE_BSWAP32(x);
    }
};

template <ByteSwappable64 T>
struct ByteSwapper<T> {
    static constexpr T
    Swap(T x)
    {
        return PULSE_BSWAP64(x);
    }
};

template <>
struct ByteSwapper<float> {
    static float
    Swap(float x)
    {
        return etl::bit_cast<float>(PULSE_BSWAP32(etl::bit_cast<uint32_t>(x)));
    }
};

template <>
struct ByteSwapper<double> {
    static double
    Swap(double x)
    {
        return etl::bit_cast<double>(PULSE_BSWAP64(etl::bit_cast<uint64_t>(x)));
    }
};

} // namespace details


template <ConstexprByteSwappable T>
constexpr T
ByteSwap(T x)
{
    return details::ByteSwapper<T>::Swap(x);
}

template <ByteSwappable T>
T
ByteSwap(T x)
{
    return details::ByteSwapper<T>::Swap(x);
}


namespace details {

template <ByteSwappable T, bool doSwap>
struct BoConverter {
    static T
    Convert(T x)
    {
        if constexpr (doSwap) {
            return ByteSwap(x);
        }
        return x;
    }
};

template <ConstexprByteSwappable T, bool doSwap>
struct BoConverter<T, doSwap> {
    static constexpr T
    Convert(T x)
    {
        if constexpr (doSwap) {
            return ByteSwap(x);
        }
        return x;
    }
};

} // namespace details


/** Helper class for byte-order-dependent value representation. The value is stored in the specified
 * byte order, so it can be directly byte-copied or accessed.
 */
template <typename T, class Converter>
class BoValue {
public:
    /** Construct value.
     *
     * @param value Value in host byte order.
     */
    BoValue(T value = 0):
        value(Converter::Convert(value))
    {}

    /** Assign new value.
     *
     * @param value Value in host byte order.
     */
    BoValue &
    operator =(T value)
    {
        this->value = Converter::Convert(value);
        return *this;
    }

    /** Cast to underlying type.
     *
     * @return Value in host byte order.
     */
    operator T() const
    {
        return Converter::Convert(value);
    }

    /** Get the value of underlying type.
     *
     * @return Value in host byte order.
     */
    T
    Get() const
    {
        return Converter::Convert(value);
    }

    /** Interpret byte buffer as a storage for underlying type and
     * return host byte order value. Caller is responsible for the size of
     * the input buffer.
     *
     * @param buffer Input buffer with original value.
     * @return Value in host byte order.
     */
    static T
    Get(const void *buffer)
    {
        return *static_cast<const BoValue *>(buffer);
    }

    /** Save value given in host order to byte buffer.
     * Caller is responsible for the size of the buffer.
     *
     * @param buffer Output buffer to save value to.
     * @param value value in host byte order.
     */
    static void
    Set(void *buffer, const T value)
    {
        *(static_cast<BoValue *>(buffer)) = value;
    }

    uint8_t *
    Bytes()
    {
        return reinterpret_cast<uint8_t *>(&value);
    }

    const uint8_t *
    Bytes() const
    {
        return reinterpret_cast<const uint8_t *>(&value);
    }

private:
    /// Stored value (in wire byte order).
    T value;
} PULSE_PACKED;


/** Little-endian value wrapper.
 * @param T Underlying primitive type.
 */
template <typename T>
using LeValue = BoValue<T, details::BoConverter<T, IsSystemBe()>>;

/** Big-endian value wrapper.
 * @param T Underlying primitive type.
 */
template <typename T>
using BeValue = BoValue<T, details::BoConverter<T, IsSystemLe()>>;


/// Predefined primitive types for little-endian byte order.
typedef LeValue<int8_t> LeInt8;
typedef LeValue<uint8_t> LeUint8;
typedef LeValue<int16_t> LeInt16;
typedef LeValue<uint16_t> LeUint16;
typedef LeValue<int32_t> LeInt32;
typedef LeValue<uint32_t> LeUint32;
typedef LeValue<int64_t> LeInt64;
typedef LeValue<uint64_t> LeUint64;
typedef LeValue<float> LeFloat;
typedef LeValue<double> LeDouble;


/// Predefined primitive types for big-endian byte order.
typedef BeValue<int8_t> BeInt8;
typedef BeValue<uint8_t> BeUint8;
typedef BeValue<int16_t> BeInt16;
typedef BeValue<uint16_t> BeUint16;
typedef BeValue<int32_t> BeInt32;
typedef BeValue<uint32_t> BeUint32;
typedef BeValue<int64_t> BeInt64;
typedef BeValue<uint64_t> BeUint64;
typedef BeValue<float> BeFloat;
typedef BeValue<double> BeDouble;


} // namespace pulse

#endif /* ENDIAN_H */
