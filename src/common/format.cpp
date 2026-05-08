#include <pulse/format.h>

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
                // Result overflow
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

} // anonymous namespace

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
                    // ':' expected
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
                    // Bad field reference
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
                // Precision value expected
                return false;
            }
            c = *p;
            int value;
            if (c == '{') {
                size_t n = ParseFieldRef(etl::string_view(p + 1, size - 1), value);
                if (n == 0) {
                    // Bad field reference
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
                    // Precision value expected
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
                // Unrecognized type
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
        // Unexpected trailing characters
        return false;
    }

    return true;
}

size_t
details::FormatTo(OutputStream &stream, size_t n, etl::string_view format,
                  etl::span<const FormatArgBase *> args)
{

    //XXX
    return 0;
}


size_t
Formatter<int>::operator()(OutputStream &stream, size_t n, int value)
{
    //XXX
    return 0;
}


size_t
StringFormatter::Format(OutputStream &stream, size_t n, const char *value, size_t size)
{
    //XXX
    return 0;
}
