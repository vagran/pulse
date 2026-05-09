#ifndef FORMAT_H
#define FORMAT_H

/*
 * Implementation for strings formatting library. The goal is to make its code size as small as
 * possible. Current implementation of `etl::format()` consumes too much code space, which is also
 * the case for most other implementations which rely too much on templates. Here we use
 * polymorphism instead. Also the goal is to not include code for types which are not actually used
 * in application format calls. To achieve this, it should not dynamically reference formatters for
 * all possible types, allowing linker garbage collector to eliminate unused functions. Extra
 * feature is the ability to provide formatters for custom user types.
 */

#include <pulse/config.h>
#include <etl/string_view.h>
#include <etl/string.h>
#include <etl/tuple.h>
#include <etl/span.h>
#include <etl/vector.h>
#include <etl/concepts.h>
#include <etl/to_string.h>


namespace pulse {

namespace fmt {

class OutputStream {
public:
    virtual void
    WriteChar(char c) = 0;

    void
    Write(etl::string_view s);
};


class BufferOutputStream: public OutputStream {
public:
    BufferOutputStream(const BufferOutputStream &) = delete;

    BufferOutputStream(char *buffer, size_t bufferSize):
        ptr(buffer),
        size(bufferSize)
    {}

    BufferOutputStream(etl::istring &out):
        ptr(out.data()),
        size(out.max_size())
    {}

    virtual void
    WriteChar(char c) override
    {
        if (size) {
            *ptr = c;
            size--;
            ptr++;
        }
    }

    size_t
    RemainingSize() const
    {
        return size;
    }

private:
    char *ptr;
    size_t size;
};


/// Parsed format specifier.
class FormatSpec {
public:
    /// Negative numbers indicate replacement field index (-2 based, -1 if index not specified).
    etl::optional<int> width, precision;
    /// -1 if not specified.
    int argId = -1;
    /// 0 if not specified.
    char fill = 0;
    /// 0 if not specified, `<`, `^` or `>` otherwise.
    char align = 0;
    /// `+`, `-` or ` `
    char sign = '-';
    /// 0 if not specified.
    char type = 0;
    /// Alternate form.
    bool alternate = false;
    bool localeSpecific = false;
    bool leadingZeros = false;

    /** Parse specifier (content between `{` and `}`). Should be called for default-constructed
     * object.
     * @return True if no errors, false if formatting error detected.
     */
    bool
    Parse(etl::string_view s);
};


/// Report error from formatters. Passed to `pulseConfig_FORMAT_ERROR` if defined.
void inline
ReportError(const char *msg);


class FormatterBase {
public:
    FormatterBase(const FormatSpec &spec):
        spec(spec)
    {}
protected:
    const FormatSpec &spec;

    /// Write the provided string applying fill, alignment and width from the format specifier.
    size_t
    AlignString(OutputStream &stream, size_t n, etl::string_view s, char defaultAlignment = '<');
};

/// Formatter for each supported type (including any custom user types) should be implemented by
// specializing this class.
template <typename T>
class Formatter {
    static_assert(false, "Formatter not implemented");

    // Each formatter should implement:

    // Formatter(const FormatSpec &spec);

    // size_t
    // operator()(OutputStream &stream, size_t n, const T &value);
};

// Use this trait to map formatter type to value type if necessary.
template <typename T>
struct FormatterTrait {
    using TFormatter = Formatter<T>;
};


namespace details {

class IntegralFormatter: public FormatterBase {
public:
    using FormatterBase::FormatterBase;

protected:
    /// @return False if error occurred.
    bool
    GetToStringSpec(etl::format_spec &toStringSpec);

    /// Format number which was converted to string using `etl::to_string()` with format specifier
    /// created by `GetToStringSpec()`.
    size_t
    FormatNumber(OutputStream &stream, size_t n, etl::string_view number);
};

class StringFormatter: public FormatterBase {
public:
    using FormatterBase::FormatterBase;

protected:
    size_t
    Format(OutputStream &stream, size_t n, const char *value, size_t size);
};

} // namespace details


template <etl::integral T>
class Formatter<T>: public details::IntegralFormatter {
public:
    using IntegralFormatter::IntegralFormatter;

    size_t
    operator()(OutputStream &stream, size_t n, T value);
};

template <>
class Formatter<etl::string_view>: public details::StringFormatter {
public:
    using StringFormatter::StringFormatter;

    size_t
    operator()(OutputStream &stream, size_t n, const etl::string_view &value)
    {
        return Format(stream, n, value.data(), value.size());
    }
};

template <>
struct FormatterTrait<const char *> {
    using TFormatter = Formatter<etl::string_view>;
};

template <size_t size>
struct FormatterTrait<char [size]> {
    using TFormatter = Formatter<etl::string_view>;
};

template <>
struct FormatterTrait<etl::istring> {
    using TFormatter = Formatter<etl::string_view>;
};

template <size_t size>
struct FormatterTrait<etl::string<size>> {
    using TFormatter = Formatter<etl::string_view>;
};


namespace details {

constexpr size_t SIZE_UNLIMITED = etl::numeric_limits<size_t>::max();

class FormatArgBase {
public:
    virtual size_t
    Format(OutputStream &stream, size_t n, const FormatSpec &spec) const = 0;

    /** Return integer value of argument which can be used as replacement field. */
    virtual etl::optional<int>
    ReplacementField() const
    {
        return etl::nullopt;
    }
};

template <typename T>
class FormatArg: public FormatArgBase {
public:
    const T &value;

    FormatArg(const T &value):
        value(value)
    {}

    virtual size_t
    Format(OutputStream &stream, size_t n, const FormatSpec &spec) const override
    {
        using TFormatter = FormatterTrait<T>::TFormatter;
        TFormatter formatter(spec);
        return formatter(stream, n, value);
    }

    virtual etl::optional<int>
    ReplacementField() const override
    {
        if constexpr (etl::is_integral_v<T>) {
            return value;
        } else {
            return etl::nullopt;
        }
    }
};

template <typename Tuple, size_t... I>
inline void
SetFormatArgs(const Tuple &argsTuple, const FormatArgBase *argsArray[], etl::index_sequence<I...>)
{
    ((argsArray[I] = &etl::get<I>(argsTuple)), ...);
}

size_t
FormatTo(OutputStream &stream, size_t n, etl::string_view format,
         etl::span<const FormatArgBase *> args);

} // namespace details


/** Write formatted string into the provided stream, accounting specified size limit. Errors if any
 * are reported through `pulseConfig_FORMAT_ERROR` if defined.
 * @return size_t Number of characters written.
 */
template <typename... TArg>
size_t
FormatTo(OutputStream &stream, size_t n, etl::string_view format, TArg &&... args)
{
    etl::tuple<details::FormatArg<etl::remove_cvref_t<TArg>>...> formatArgs(args...);
    const details::FormatArgBase *argsArray[sizeof...(args)];
    details::SetFormatArgs(formatArgs, argsArray, etl::make_index_sequence<sizeof...(args)>());
    return details::FormatTo(stream, n, format, etl::span(argsArray, sizeof...(args)));
}

/** Write formatted string into the provided stream.
 * @return size_t Number of characters written.
 */
template <typename... TArg>
size_t
FormatTo(OutputStream &stream, etl::string_view format, TArg &&... args)
{
    return FormatTo(stream, details::SIZE_UNLIMITED, format, etl::forward<TArg>(args)...);
}

/** Write formatted string into the provided string buffer.
 * @return size_t Number of characters written.
 */
template <typename... TArg>
size_t
FormatTo(etl::istring &out, etl::string_view format, TArg &&... args)
{
    BufferOutputStream stream(out);
    size_t size = FormatTo(stream, stream.RemainingSize(), format, etl::forward<TArg>(args)...);
    out.uninitialized_resize(size);
    return size;
}

#ifdef pulseConfig_FORMAT_ERROR
void inline
ReportError(const char *msg)
{
    pulseConfig_FORMAT_ERROR(msg);
}
#else
void inline
ReportError(const char *)
{}
#endif

template <etl::integral T>
size_t
Formatter<T>::operator()(OutputStream &stream, size_t n, T value)
{
    etl::format_spec toStringSpec;
    if (!GetToStringSpec(toStringSpec)) {
        return 0;
    }
    // Maximal size is binary representation with sign and `0b` prefix.
    etl::string<sizeof(T) * 8 + 3> s;
    etl::to_string(value, s, toStringSpec);
    return FormatNumber(stream, n, s);
}

} // namespace fmt

} // namespace pulse

#endif /* FORMAT_H */
