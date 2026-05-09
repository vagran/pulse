#include <pulse/format.h>
#include <pulse/details/common.h>
#include <etl/to_string.h>


using namespace pulse::fmt;


namespace {

/// @return Number of characters consumed.
size_t
ParseNumber(etl::string_view s, int &result)
{
    result = 0;
    size_t size = 0;
    for (auto it = s.begin(); it < s.end(); it++, size++) {
        if (*it >= '0' && *it <= '9') {
            int value = result * 10 + static_cast<int>(*it - '0');
            if (value < result) {
                ReportError("Number overflow in format specifier");
                return 0;
            }
            result = value;
        } else {
            break;
        }
    }
    return size;
}

/// Parse from the first character after '{'. `result` will contain -1 if no index, -2-based index
/// otherwise. Trailing `}` always consumed.
/// @return Number of characters consumed.
size_t
ParseFieldRef(etl::string_view s, int &result)
{
    int value;
    size_t n = ParseNumber(s, value);
    if (s.size() <= n || s[n] != '}') {
        return 0;
    }
    if (n == 0) {
        result = -1;
    } else {
        result = -value - 2;
    }
    return n + 1;
}

/// Find where `{...}` expression ends.
/// @param p Pointer to first character after opening `{`.
/// @param size Number of available characters.
/// @return Pointer to closing `}` character, null if not found.
const char *
FindFormatSpecEnd(const char *p, size_t size)
{
    int nesting = 0;
    for (; size; p++, size--) {
        char c = *p;
        if (c == '{') {
            nesting++;
        } else if (c == '}') {
            if (nesting) {
                nesting--;
            } else {
                return p;
            }
        }
    }
    return nullptr;
}

} // anonymous namespace

void
OutputStream::Write(etl::string_view s)
{
    for (char c: s) {
        WriteChar(c);
    }
}

void
OutputStream::Write(StringProvider &s)
{
    for (size_t n = s.Remaining(); n > 0; n--) {
        WriteChar(s.Next());
    }
}

bool
FormatSpec::Parse(etl::string_view s)
{
    enum class State {
        ARG_ID,
        FILL_ALIGN,
        SIGN,
        ALTERNATE,
        LEADING_ZEROS,
        WIDTH,
        PRECISION,
        LOCALE,
        TYPE,
        DONE
    };

    State state = State::ARG_ID;
    size_t size = s.size();
    const char *p = s.data();

    while (size && state != State::DONE) {
        char c = *p;
        switch (state) {

        case State::ARG_ID: {
            int value;
            size_t n = ParseNumber(etl::string_view(p, size), value);
            if (n) {
                argId = value;
                size -= n;
                p += n;
            }
            if (size) {
                if (*p == ':') {
                    p++;
                    size--;
                    state = State::FILL_ALIGN;
                    continue;
                } else {
                    ReportError("':' expected");
                    return false;
                }
            }
            break;
        }

        case State::FILL_ALIGN:
            if (c == '<' || c == '^' || c == '>') {
                align = c;
                p++;
                size--;
            } else if (size > 1) {
                char a = p[1];
                if (a == '<' || a == '^' || a == '>') {
                    align = a;
                    fill = c;
                    p += 2;
                    size -= 2;
                }
            }
            state = State::SIGN;
            continue;

        case State::SIGN:
            if (c == ' ' || c == '+' || c == '-') {
                sign = c;
                p++;
                size--;
            }
            state = State::ALTERNATE;
            continue;

        case State::ALTERNATE:
            if (c == '#') {
                alternate = true;
                p++;
                size--;
            }
            state = State::LEADING_ZEROS;
            continue;

        case State::LEADING_ZEROS:
            if (c == '0') {
                if (align == 0) {
                    leadingZeros = true;
                }
                p++;
                size--;
            }
            state = State::WIDTH;
            continue;


        case State::WIDTH: {
            int value;
            if (c == '{') {
                size_t n = ParseFieldRef(etl::string_view(p + 1, size - 1), value);
                if (n == 0) {
                    ReportError("Bad field reference");
                    return false;
                }
                width = value;
                p += n + 1;
                size -= n + 1;
            } else {
                size_t n = ParseNumber(etl::string_view(p, size), value);
                if (n) {
                    width = value;
                    p += n;
                    size -= n;
                }
            }
            state = State::PRECISION;
            continue;
        }

        case State::PRECISION: {
            if (c != '.') {
                state = State::LOCALE;
                continue;
            }
            p++;
            size--;
            if (size == 0) {
                ReportError("Precision value expected");
                return false;
            }
            c = *p;
            int value;
            if (c == '{') {
                size_t n = ParseFieldRef(etl::string_view(p + 1, size - 1), value);
                if (n == 0) {
                    ReportError("Bad field reference");
                    return false;
                }
                precision = value;
                p += n + 1;
                size -= n + 1;
            } else {
                size_t n = ParseNumber(etl::string_view(p, size), value);
                if (n) {
                    precision = value;
                    p += n;
                    size -= n;
                } else {
                    ReportError("Precision value expected");
                    return false;
                }
            }
            state = State::LOCALE;
            continue;
        }

        case State::LOCALE:
            if (c == 'L') {
                localeSpecific = true;
                p++;
                size--;
            }
            state = State::TYPE;
            continue;

        case State::TYPE:
            if (c == 's' || c == 'b' || c == 'B' || c == 'd' || c == 'o' || c == 'x' || c == 'X' ||
                c == 'a' || c == 'A' || c == 'e' || c == 'E' || c == 'f' || c == 'F' || c == 'g' ||
                c == 'G' || c == 'p' || c == 'P' || c == 'c') {

                type = c;
                p++;
                size--;
            } else {
                ReportError("Unrecognized type");
                return false;
            }
            state = State::DONE;
            continue;

        case State::DONE:
            // Should not be reached
            return false;
        }
    }

    if (size) {
        ReportError("Unexpected trailing characters");
        return false;
    }

    return true;
}

size_t
details::FormatTo(OutputStream &stream, size_t n, etl::string_view format,
                  etl::span<const FormatArgBase *> args)
{
    size_t curArgIdx = 0;
    size_t resultSize = 0;
    size_t size = format.size();
    const char *p = format.data();

    /// @param idx Argument index to check, -1 to obtain next auto argument.
    /// @return True if obtained, false if violation.
    auto GetArg = [&](int &idx) -> bool {
        if (idx < 0) {
            if (curArgIdx == etl::numeric_limits<size_t>::max()) {
                return false;
            }
            idx = curArgIdx;
            curArgIdx++;
        } else {
            if (curArgIdx != 0 && curArgIdx != etl::numeric_limits<size_t>::max()) {
                return false;
            }
            curArgIdx = etl::numeric_limits<size_t>::max();
        }
        return true;
    };

    /// If field value is argument reference, replace it with its value.
    auto ResolveField = [&](etl::optional<int> &value) -> bool {
        if (!value) {
            return true;
        }
        if (*value >= 0) {
            // Immediate value specified
            return true;
        }
        int argIdx = *value;
        if (argIdx < -1) {
            argIdx = -argIdx - 2;
        }
        if (!GetArg(argIdx)) {
            ReportError("Mixing manual and automatic indexing");
            return false;
        }
        if (static_cast<size_t>(argIdx) >= args.size()) {
            ReportError("Argument index out of bounds");
            return false;
        }
        etl::optional<int> argValue = args[argIdx]->ReplacementField();
        if (!argValue) {
            ReportError("Argument is not suitable for replacement field");
            return false;
        }
        if (*argValue < 0) {
            ReportError("Negative value for replacement field");
            return false;
        }
        value = argValue;
        return true;
    };

    while (size && n) {
        char c = *p;
        if (c == '{') {
            if (size > 1) {
                if (p[1] == '{') {
                    stream.WriteChar('{');
                    resultSize++;
                    p += 2;
                    size -= 2;
                    if (n != etl::numeric_limits<size_t>::max()) {
                        n--;
                    }
                    continue;

                } else {
                    const char *end = FindFormatSpecEnd(p + 1, size - 1);
                    if (!end) {
                        ReportError("Unclosed format specified");
                        break;
                    }

                    FormatSpec spec;
                    if (!spec.Parse(etl::string_view(p + 1, end))) {
                        ReportError("Format specifier parsing failed");
                        size_t numWritten = etl::min(static_cast<size_t>(end - p + 1), n);
                        stream.Write(etl::string_view(p, numWritten));
                        size -= numWritten;
                        if (n != etl::numeric_limits<size_t>::max()) {
                            n -= numWritten;
                        }
                        p = end + 1;
                        continue;
                    }
                    int argIdx = spec.argId;
                    if (!GetArg(argIdx)) {
                        ReportError("Mixing manual and automatic indexing");
                        break;
                    }
                    if (static_cast<size_t>(argIdx) >= args.size()) {
                        ReportError("Argument index out of bounds");
                        break;
                    }
                    // Resolve width and precision references if any
                    if (!ResolveField(spec.width)) {
                        ReportError("Width field resolving failed");
                        break;
                    }
                    if (!ResolveField(spec.precision)) {
                        ReportError("Precision field resolving failed");
                        break;
                    }
                    size_t numWritten = args[argIdx]->Format(stream, n, spec);
                    PULSE_ASSERT(numWritten <= n);
                    resultSize += numWritten;
                    if (n != etl::numeric_limits<size_t>::max()) {
                        n -= numWritten;
                    }
                    size -= end - p + 1;
                    p = end + 1;
                    continue;
                }

            } else {
                stream.WriteChar('{');
                resultSize++;
                if (n != etl::numeric_limits<size_t>::max()) {
                    n--;
                }
                ReportError("Trailing unescaped '{'");
                break;
            }

        } else if (c == '}') {
            if (size > 1) {
                if (p[1] == '}') {
                    stream.WriteChar('}');
                    resultSize++;
                    p += 2;
                    size -= 2;
                    if (n != etl::numeric_limits<size_t>::max()) {
                        n--;
                    }
                    continue;
                } else {
                    stream.WriteChar('}');
                    resultSize++;
                    p++;
                    size--;
                    if (n != etl::numeric_limits<size_t>::max()) {
                        n--;
                    }
                    ReportError("Unexpected '}'");
                    continue;
                }
            } else {
                stream.WriteChar('}');
                resultSize++;
                p++;
                size--;
                if (n != etl::numeric_limits<size_t>::max()) {
                    n--;
                }
                ReportError("Trailing unescaped '}'");
                break;
            }
        }
        stream.WriteChar(c);
        resultSize++;
        p++;
        size--;
        if (n != etl::numeric_limits<size_t>::max()) {
            n--;
        }
    }
    return resultSize;
}

size_t
FormatterBase::AlignString(OutputStream &stream, size_t n, StringProvider &s, char defaultAlignment)
{
    size_t numWritten = 0;
    if (!spec.width || static_cast<size_t>(*spec.width) <= s.Remaining()) {
        // Alignment and fill does not matter if width is not specified or less than string width.
        numWritten = etl::min(n, s.Remaining());
        s.Truncate(numWritten);
        stream.Write(s);
        return numWritten;
    }

    size_t margin = *spec.width - s.Remaining();
    char fill = spec.fill ? spec.fill : ' ';

    size_t leftMargin;
    char align = spec.align ? spec.align : defaultAlignment;
    if (align == '>') {
        leftMargin = margin;
    } else if (align == '^') {
        leftMargin = margin / 2;
    } else {
        leftMargin = 0;
    }

    for (size_t i = 0; i < leftMargin && n; i++) {
        stream.WriteChar(fill);
        numWritten++;
        n--;
    }

    if (n == 0) {
        return numWritten;
    }

    size_t writeSize = etl::min(n, s.Remaining());
    s.Truncate(writeSize);
    stream.Write(s);
    n -= writeSize;
    numWritten += writeSize;

    if (n == 0) {
        return numWritten;
    }

    size_t rightMargin = margin - leftMargin;
    for (size_t i = 0; i < rightMargin && n; i++) {
        stream.WriteChar(fill);
        numWritten++;
        n--;
    }

    return numWritten;
}

bool
details::IntegralFormatter::GetToStringSpec(etl::format_spec &toStringSpec)
{
    if (spec.precision) {
        ReportError("Precision specified for integral value");
        return false;
    }
    switch (spec.type) {
    case 0:
    case 'd':
        toStringSpec.base(10);
        break;
    case 'b':
    case 'B':
        toStringSpec.base(2);
        break;
    case 'o':
        toStringSpec.base(8);
        break;
    case 'x':
    case 'X':
        toStringSpec.base(16);
        toStringSpec.upper_case(spec.type == 'X');
        break;
    default:
        ReportError("Bad type for integral argument");
        return false;
    }
    return true;
}

namespace {

class IntegralNumberStringProvider: public StringProvider {
public:
    IntegralNumberStringProvider(etl::string_view number, int sign, const FormatSpec &spec):
        StringProvider(GetSize(number.size(), sign, spec)),
        spec(spec),
        pNumber(number.data()),
        numLen(number.size()),
        sign(sign)
    {}

protected:
    virtual char
    GetNext() override;

private:
    enum class State {
        SIGN,
        PREFIX,
        ZEROS,
        NUMBER
    };

    const FormatSpec &spec;
    const char *pNumber;
    size_t numLen;
    size_t stateLen = 0;
    State state = State::SIGN;
    const int8_t sign;
    char prefix = 0;

    static size_t
    GetSize(size_t numLen, int sign, const FormatSpec &spec);
};

size_t
IntegralNumberStringProvider::GetSize(size_t numLen, int sign, const FormatSpec &spec)
{
    size_t size = 0;

    if (spec.sign != '-' || sign < 0) {
        size++;
    }

    if (spec.alternate) {
        if (spec.type == 'o') {
            size++;
        } else if (spec.type == 'b' || spec.type == 'B' || spec.type == 'x' || spec.type == 'X') {
            size += 2;
        }
    }

    size += numLen;

    if (spec.width && spec.leadingZeros && !spec.align && static_cast<size_t>(*spec.width) > size) {
        size = *spec.width;
    }

    return size;
}

char
IntegralNumberStringProvider::GetNext()
{
    while (true) {
        switch (state) {

        case State::SIGN:
            state = State::PREFIX;
            if (sign < 0) {
                return '-';
            }
            if (spec.sign != '-') {
                if (sign > 0 && spec.sign == '+') {
                    return '+';
                }
                return ' ';
            }
            continue;

        case State::PREFIX:
            if (!spec.alternate) {
                state = State::ZEROS;
                continue;
            }
            if (prefix) {
                state = State::ZEROS;
                return prefix;
            }
            switch (spec.type) {
            case 'o':
                state = State::ZEROS;
                return '0';
            case 'b':
            case 'B':
            case 'x':
            case 'X':
                prefix = spec.type;
                return '0';
            }
            state = State::ZEROS;
            continue;

        case State::ZEROS:
            if (stateLen) {
                stateLen--;
                if (!stateLen) {
                    state = State::NUMBER;
                    stateLen = numLen;
                }
                return '0';
            }
            // `remaining` is decremented before this method is called.
            if (spec.leadingZeros && !spec.align && remaining + 1 > numLen) {
                stateLen = remaining + 1 - numLen;
                continue;
            }
            state = State::NUMBER;
            stateLen = numLen;
            continue;

        case State::NUMBER:
            PULSE_ASSERT(stateLen);
            if (stateLen) {
                stateLen--;
                return *pNumber++;
            }
            return '?';
        }
    }
}

} // anonymous namespace

size_t
details::IntegralFormatter::FormatNumber(OutputStream &stream, size_t n, etl::string_view number,
                                         int sign)
{
    IntegralNumberStringProvider provider(number, sign, spec);
    return AlignString(stream, n, provider, '>');
}

size_t
details::StringFormatter::Format(OutputStream &stream, size_t n, const char *value, size_t size)
{
    if (spec.precision) {
        if (static_cast<size_t>(*spec.precision) < size) {
            size = *spec.precision;
        }
    }
    return AlignString(stream, n, etl::string_view(value, size));
}
