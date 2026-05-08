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

#include <etl/string_view.h>
#include <etl/string.h>
#include <etl/tuple.h>
#include <etl/span.h>
#include <etl/vector.h>


namespace pulse {

namespace fmt {

class OutputStream {
public:
    virtual void
    WriteChar(char c) = 0;
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


class FormatterBase {
public:
    FormatterBase(const FormatSpec &spec):
        spec(spec)
    {}
protected:
    const FormatSpec &spec;
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

template <>
class Formatter<int>: public FormatterBase {
public:
    using FormatterBase::FormatterBase;

    size_t
    operator()(OutputStream &stream, size_t n, int value);
};


class StringFormatter: public FormatterBase {
public:
    using FormatterBase::FormatterBase;

protected:
    size_t
    Format(OutputStream &stream, size_t n, const char *value, size_t size);
};

template <>
class Formatter<const char *>: public StringFormatter {
public:
    using StringFormatter::StringFormatter;

    size_t
    operator()(OutputStream &stream, size_t n, const char *value)
    {
        return Format(stream, n, value, strlen(value));
    }
};

template <size_t size>
struct FormatterTrait<char [size]> {
    using TFormatter = Formatter<const char *>;
};

template <>
class Formatter<etl::istring>: public StringFormatter {
public:
    using StringFormatter::StringFormatter;

    size_t
    operator()(OutputStream &stream, size_t n, const etl::istring &value)
    {
        return Format(stream, n, value.data(), value.size());
    }
};

template <size_t size>
struct FormatterTrait<etl::string<size>> {
    using TFormatter = Formatter<etl::istring>;
};

namespace details {

constexpr size_t SIZE_UNLIMITED = etl::numeric_limits<size_t>::max();

class FormatArgBase {
public:
    virtual size_t
    Format(OutputStream &stream, size_t n, const FormatSpec &spec) = 0;

    /** Return integer value of argument which can be used as replacement field. */
    virtual etl::optional<int>
    ReplacementField()
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
    Format(OutputStream &stream, size_t n, const FormatSpec &spec) override
    {
        using TFormatter = FormatterTrait<T>::TFormatter;
        TFormatter formatter(spec);
        return formatter(stream, n, value);
    }

    virtual etl::optional<int>
    ReplacementField() override
    {
        if constexpr (etl::is_integral_v<T>) {
            return value;
        } else {
            return etl::nullopt;
        }
    }
};

template <typename Tuple, std::size_t... I>
inline void
SetFormatArgs(const Tuple &argsTuple, const FormatArgBase *argsArray[], etl::index_sequence<I...>)
{
    ((argsArray[I] = &etl::get<I>(argsTuple)), ...);
}

size_t
FormatTo(OutputStream &stream, size_t n, etl::string_view format,
         etl::span<const FormatArgBase *> args);

} // namespace details


/** Write formatted string into the provided stream, accounting specified size limit.
 * @return size_t Number of characters written. Zero is returned and nothing is written in case of
 *  any paremeters error.
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
 * @return size_t Number of characters written. Zero is returned and nothing is written in case of
 *  any paremeters error.
 */
template <typename... TArg>
size_t
FormatTo(OutputStream &stream, etl::string_view format, TArg &&... args)
{
    return FormatTo(stream, details::SIZE_UNLIMITED, format, etl::forward<TArg>(args)...);
}

/** Write formatted string into the provided string buffer.
 * @return size_t Number of characters written. Zero is returned and nothing is written in case of
 *  any paremeters error.
 */
template <typename... TArg>
size_t
FormatTo(etl::istring &out, etl::string_view format, TArg &&... args)
{
    return FormatTo(BufferOutputStream(out), out.max_size(), format, etl::forward<TArg>(args)...);
}

} // namespace fmt

} // namespace pulse

#endif /* FORMAT_H */
