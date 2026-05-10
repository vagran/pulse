#include <common.h>
#include <catch2/catch_test_macros.hpp>
#include <pulse/format.h>
#include <pulse/config.h>


using namespace pulse;


namespace {

class StringOutputStream: public fmt::OutputStream {
public:
    StringOutputStream(const StringOutputStream &) = delete;

    StringOutputStream() = default;

    virtual void
    WriteChar(char c) override
    {
        out += c;
    }

    std::string
    str() const
    {
        return out;
    }

    size_t
    size() const
    {
        return out.size();
    }

private:
    std::string out;
};

bool errorSeen = false;

class CustomType {
public:
    const int value;

    CustomType(int value):
        value(value)
    {};
};

} // anonymous namespace

template <>
class pulse::fmt::Formatter<CustomType>: public FormatterBase {
public:
    using FormatterBase::FormatterBase;

    size_t
    operator()(OutputStream &stream, size_t n, const CustomType &value);
};

size_t
pulse::fmt::Formatter<CustomType>::operator()(OutputStream &stream, size_t n,
                                              const CustomType &value)
{
    etl::string<63> s;
    if (spec.type == 'z') {
        pulse::fmt::FormatTo(s, "custom_{}", value.value);
    } else if (spec.type == 'Z') {
        pulse::fmt::FormatTo(s, "CUSTOM_{}", value.value);
    } else {
        pulse::fmt::ReportError("Bad type for CustomType");
        return 0;
    }
    return AlignString(stream, n, s);
}

void
FormatError(const char *msg)
{
    UNSCOPED_INFO("FormatError: " << msg);
    errorSeen = true;
}


TEST_CASE("FormatSpec::Parse") {

    fmt::FormatSpec spec;
    errorSeen = false;

    SECTION("Empty specifier") {
        REQUIRE(spec.Parse(""));

        CHECK(spec.argId == -1);
        CHECK_FALSE(spec.width.has_value());
        CHECK_FALSE(spec.precision.has_value());
        CHECK(spec.fill == 0);
        CHECK(spec.align == 0);
        CHECK(spec.sign == '-');
        CHECK(spec.type == 0);
        CHECK_FALSE(spec.alternate);
        CHECK_FALSE(spec.localeSpecific);
        CHECK_FALSE(spec.leadingZeros);
        CHECK_FALSE(errorSeen);
    }

    SECTION("Argument index only (0)") {
        REQUIRE(spec.Parse("0"));

        CHECK(spec.argId == 0);
        CHECK_FALSE(errorSeen);
    }

    SECTION("Argument index only") {
        REQUIRE(spec.Parse("2"));

        CHECK(spec.argId == 2);
        CHECK_FALSE(errorSeen);
    }

    SECTION("Width only") {
        REQUIRE(spec.Parse(":10"));

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 10);
        CHECK_FALSE(errorSeen);
    }

    SECTION("Left alignment") {
        REQUIRE(spec.Parse(":<10"));

        CHECK(spec.align == '<');
        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 10);
        CHECK_FALSE(errorSeen);
    }

    SECTION("Right alignment with fill") {
        REQUIRE(spec.Parse(":_>8"));

        CHECK(spec.fill == '_');
        CHECK(spec.align == '>');
        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 8);
        CHECK_FALSE(errorSeen);
    }

    SECTION("Center alignment with fill") {
        REQUIRE(spec.Parse(":*^12"));

        CHECK(spec.fill == '*');
        CHECK(spec.align == '^');
        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 12);
        CHECK_FALSE(errorSeen);
    }

    SECTION("Sign plus") {
        REQUIRE(spec.Parse(":+"));

        CHECK(spec.sign == '+');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Sign minus") {
        REQUIRE(spec.Parse(":-"));

        CHECK(spec.sign == '-');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Sign space") {
        REQUIRE(spec.Parse(": "));

        CHECK(spec.sign == ' ');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Alternate form") {
        REQUIRE(spec.Parse(":#x"));

        CHECK(spec.alternate);
        CHECK(spec.type == 'x');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Leading zeros") {
        REQUIRE(spec.Parse(":08d"));

        CHECK(spec.leadingZeros);
        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 8);
        CHECK(spec.type == 'd');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Precision only") {
        REQUIRE(spec.Parse(":.5f"));

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == 5);
        CHECK(spec.type == 'f');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Width and precision") {
        REQUIRE(spec.Parse(":10.3f"));

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 10);

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == 3);

        CHECK(spec.type == 'f');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Locale specific") {
        REQUIRE(spec.Parse(":L"));

        CHECK(spec.localeSpecific);
        CHECK_FALSE(errorSeen);
    }

    SECTION("Complex full specifier") {
        REQUIRE(spec.Parse("1:*^+#012.5Lf"));

        CHECK(spec.argId == 1);
        CHECK(spec.fill == '*');
        CHECK(spec.align == '^');
        CHECK(spec.sign == '+');
        CHECK(spec.alternate);
        // Leading zeros ignored due to alignment present
        CHECK(!spec.leadingZeros);

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 12);

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == 5);

        CHECK(spec.localeSpecific);
        CHECK(spec.type == 'f');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Reject invalid alignment") {
        CHECK_FALSE(spec.Parse(":?10"));
        CHECK(errorSeen);
    }

    SECTION("Reject invalid type") {
        CHECK_FALSE(spec.Parse(":10!"));
        CHECK(errorSeen);
    }

    SECTION("Reject malformed precision") {
        CHECK_FALSE(spec.Parse(":.f"));
        CHECK(errorSeen);
    }

    SECTION("Reject duplicate precision") {
        CHECK_FALSE(spec.Parse(":10.2.3f"));
        CHECK(errorSeen);
    }

    SECTION("Reject random garbage") {
        CHECK_FALSE(spec.Parse("abc$%^"));
        CHECK(errorSeen);
    }

    SECTION("Width from argument reference") {
        REQUIRE(spec.Parse(":{3}"));

        REQUIRE(spec.width.has_value());
        // Negative values encode replacement field index (-2 based).
        CHECK(*spec.width == -5); // arg 3 => -(3 + 2)
        CHECK_FALSE(spec.precision.has_value());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Width from automatic argument reference") {
        REQUIRE(spec.Parse(":{}"));

        REQUIRE(spec.width.has_value());
        // Automatic argument index = -1
        CHECK(*spec.width == -1);
        CHECK_FALSE(spec.precision.has_value());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Precision from argument reference") {
        REQUIRE(spec.Parse(":.{2}f"));

        CHECK_FALSE(spec.width.has_value());

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == -4); // arg 2 => -(2 + 2)

        CHECK(spec.type == 'f');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Precision from automatic argument reference") {
        REQUIRE(spec.Parse(":.{}f"));

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == -1);

        CHECK(spec.type == 'f');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Width and precision both from argument references") {
        REQUIRE(spec.Parse(":{1}.{2}f"));

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == -3); // arg 1 => -(1 + 2)

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == -4); // arg 2 => -(2 + 2)

        CHECK(spec.type == 'f');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Mixed numeric width and argument precision") {
        REQUIRE(spec.Parse(":10.{4}g"));

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 10);

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == -6); // arg 4 => -(4 + 2)

        CHECK(spec.type == 'g');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Mixed argument width and numeric precision") {
        REQUIRE(spec.Parse(":{2}.5f"));

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == -4); // arg 2 => -(2 + 2)

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == 5);

        CHECK(spec.type == 'f');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Reject malformed width argument reference") {
        CHECK_FALSE(spec.Parse(":{abc}"));
        CHECK(errorSeen);
    }

    SECTION("Reject malformed precision argument reference") {
        CHECK_FALSE(spec.Parse(":.{x}f"));
        CHECK(errorSeen);
    }

    SECTION("Reject missing closing brace in width argument reference") {
        CHECK_FALSE(spec.Parse(":{2"));
        CHECK(errorSeen);
    }

    SECTION("Reject missing closing brace in precision argument reference") {
        CHECK_FALSE(spec.Parse(":.{3f"));
        CHECK(errorSeen);
    }

    SECTION("Reject empty manual width reference") {
        CHECK_FALSE(spec.Parse(":{ }"));
        CHECK(errorSeen);
    }

    SECTION("Dynamic width from explicit zero argument") {
        REQUIRE(spec.Parse(":{0}"));

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == -2); // arg 0 => -(0 + 2)
        CHECK_FALSE(spec.precision.has_value());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic precision from automatic argument") {
        REQUIRE(spec.Parse(":.{}"));

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == -1);
        CHECK_FALSE(spec.width.has_value());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Alternate hexadecimal with zero padding") {
        REQUIRE(spec.Parse(":#08x"));

        CHECK(spec.alternate);
        CHECK(spec.leadingZeros);

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 8);

        CHECK(spec.type == 'x');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Custom fill with left alignment") {
        REQUIRE(spec.Parse(":*<10"));

        CHECK(spec.fill == '*');
        CHECK(spec.align == '<');

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 10);
        CHECK_FALSE(errorSeen);
    }

    SECTION("Character type") {
        REQUIRE(spec.Parse(":c"));

        CHECK(spec.type == 'c');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Reject incomplete width field reference") {
        CHECK_FALSE(spec.Parse(":{"));
        CHECK(errorSeen);
    }

    SECTION("Reject stray closing brace") {
        CHECK_FALSE(spec.Parse(":}"));
        CHECK(errorSeen);
    }

    SECTION("Reject malformed nested braces") {
        CHECK_FALSE(spec.Parse(":{{}"));
        CHECK(errorSeen);
    }

    SECTION("Reject malformed precision closing brace") {
        CHECK_FALSE(spec.Parse(":.}"));
        CHECK(errorSeen);
    }

    SECTION("Reject duplicate locale specifier") {
        CHECK_FALSE(spec.Parse(":LxL"));
        CHECK(errorSeen);
    }

    SECTION("Reject overflowing field reference index") {
        CHECK_FALSE(spec.Parse(":{999999999999999999}"));
        CHECK(errorSeen);
    }

    SECTION("Reject overflowing precision field reference index") {
        CHECK_FALSE(spec.Parse(":.{999999999999999999}f"));
        CHECK(errorSeen);
    }
}


TEST_CASE("FormatTo integer formatting") {
    using namespace pulse::fmt;
    errorSeen = false;

    SECTION("Default decimal formatting") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{}", 42) == 2);
        CHECK(out == "42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Negative signed integer") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{}", -42) == 3);
        CHECK(out == "-42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Zero value") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{}", 0) == 1);
        CHECK(out == "0");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Explicit positive sign") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:+}", 42) == 3);
        CHECK(out == "+42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Space sign for positive") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{: }", 42) == 3);
        CHECK(out == " 42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Space sign for negative") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{: }", -42) == 3);
        CHECK(out == "-42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Binary formatting") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:b}", 10) == 4);
        CHECK(out == "1010");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Binary alternate form") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:#b}", 10) == 6);
        CHECK(out == "0b1010");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Upper binary alternate form") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:#B}", 10) == 6);
        CHECK(out == "0B1010");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Octal formatting") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:o}", 10) == 2);
        CHECK(out == "12");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Octal alternate form") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:#o}", 10) == 3);
        CHECK(out == "012");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Hex lowercase") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:x}", 255) == 2);
        CHECK(out == "ff");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Hex uppercase") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:X}", 255) == 2);
        CHECK(out == "FF");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Hex alternate lowercase") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:#x}", 255) == 4);
        CHECK(out == "0xff");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Hex alternate uppercase") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:#X}", 255) == 4);
        CHECK(out == "0XFF");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Width right align default") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:5}", 42) == 5);
        CHECK(out == "   42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Width left align") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:<5}", 42) == 5);
        CHECK(out == "42   ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Width center align") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:^5}", 42) == 5);
        CHECK(out == " 42  ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Custom fill right align") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:_>5}", 42) == 5);
        CHECK(out == "___42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Custom fill left align") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:_<5}", 42) == 5);
        CHECK(out == "42___");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Custom fill center align") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:_^5}", 42) == 5);
        CHECK(out == "_42__");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Leading zero padding") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:05}", 42) == 5);
        CHECK(out == "00042");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Leading zero padding with sign") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:+05}", 42) == 5);
        CHECK(out == "+0042");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Leading zero padding negative") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:05}", -42) == 5);
        CHECK(out == "-0042");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Leading zero padding with alternate hex") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:#06x}", 255) == 6);
        CHECK(out == "0x00ff");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic width") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:{}}", 42, 5) == 5);
        CHECK(out == "   42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic width left align") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:<{}}", 42, 5) == 5);
        CHECK(out == "42   ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Argument reordering") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{1} {0}", 10, 20) == 5);
        CHECK(out == "20 10");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Repeated argument") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{0} {0} {0}", 7) == 5);
        CHECK(out == "7 7 7");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Signed minimum int") {
        etl::string<64> out;
        int value = std::numeric_limits<int>::min();
        FormatTo(out, "{}", value);
        CHECK(std::string(out.data(), out.size()) == std::to_string(value));
        CHECK_FALSE(errorSeen);
    }

    SECTION("Signed maximum int") {
        etl::string<64> out;
        int value = std::numeric_limits<int>::max();
        size_t n = FormatTo(out, "{}", value);
        CHECK(n == out.size());
        CHECK(std::string(out.data(), out.size()) == std::to_string(value));
        CHECK_FALSE(errorSeen);
    }

    SECTION("Unsigned maximum") {
        etl::string<64> out;
        unsigned value = std::numeric_limits<unsigned>::max();
        size_t n = FormatTo(out, "{}", value);
        CHECK(n == out.size());
        CHECK(std::string(out.data(), out.size()) == std::to_string(value));
        CHECK_FALSE(errorSeen);
    }

    SECTION("Zero with alternate hex") {
        etl::string<64> out;
        FormatTo(out, "{:#x}", 0);
        CHECK((out == "0" || out == "0x0"));
        CHECK_FALSE(errorSeen);
    }

    SECTION("Zero with alternate binary") {
        etl::string<64> out;
        FormatTo(out, "{:#b}", 0);
        CHECK((out == "0" || out == "0b0"));
        CHECK_FALSE(errorSeen);
    }

    SECTION("Multiple integers in one format string") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{} {} {}", 1, 22, 333) == 8);
        CHECK(out == "1 22 333");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Literal braces escaped") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{{{}}}", 42) == 4);
        CHECK(out == "{42}");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Buffer truncation behavior with small buffer") {
        etl::string<4> out;
        size_t written = FormatTo(out, "{}", 123456);
        CHECK(written <= out.max_size());
        CHECK(std::string(out.data(), out.size()) == "1234");
        CHECK_FALSE(errorSeen);
    }

    SECTION("int16_t default decimal") {
        etl::string<64> out;
        int16_t value = static_cast<int16_t>(12345);

        CHECK(FormatTo(out, "{}", value) == 5);
        CHECK(out == "12345");
        CHECK_FALSE(errorSeen);
    }

    SECTION("int16_t negative value") {
        etl::string<64> out;
        int16_t value = static_cast<int16_t>(-12345);

        CHECK(FormatTo(out, "{}", value) == 6);
        CHECK(out == "-12345");
        CHECK_FALSE(errorSeen);
    }

    SECTION("int16_t minimum value") {
        etl::string<64> out;
        int16_t value = std::numeric_limits<int16_t>::min();

        CHECK(FormatTo(out, "{}", value) == 6);
        CHECK(out == "-32768");
        CHECK_FALSE(errorSeen);
    }

    SECTION("int16_t maximum value") {
        etl::string<64> out;
        int16_t value = std::numeric_limits<int16_t>::max();

        CHECK(FormatTo(out, "{}", value) == 5);
        CHECK(out == "32767");
        CHECK_FALSE(errorSeen);
    }

    SECTION("int16_t hexadecimal alternate") {
        etl::string<64> out;
        int16_t value = static_cast<int16_t>(255);

        CHECK(FormatTo(out, "{:#x}", value) == 4);
        CHECK(out == "0xff");
        CHECK_FALSE(errorSeen);
    }

    SECTION("int16_t binary formatting") {
        etl::string<64> out;
        int16_t value = static_cast<int16_t>(10);

        CHECK(FormatTo(out, "{:b}", value) == 4);
        CHECK(out == "1010");
        CHECK_FALSE(errorSeen);
    }

    SECTION("uint16_t default decimal") {
        etl::string<64> out;
        uint16_t value = static_cast<uint16_t>(65535);

        CHECK(FormatTo(out, "{}", value) == 5);
        CHECK(out == "65535");
        CHECK_FALSE(errorSeen);
    }

    SECTION("uint16_t hexadecimal uppercase") {
        etl::string<64> out;
        uint16_t value = static_cast<uint16_t>(65535);

        CHECK(FormatTo(out, "{:X}", value) == 4);
        CHECK(out == "FFFF");
        CHECK_FALSE(errorSeen);
    }

    SECTION("uint16_t alternate binary") {
        etl::string<64> out;
        uint16_t value = static_cast<uint16_t>(15);

        CHECK(FormatTo(out, "{:#b}", value) == 6);
        CHECK(out == "0b1111");
        CHECK_FALSE(errorSeen);
    }

    SECTION("uint16_t width and zero padding") {
        etl::string<64> out;
        uint16_t value = static_cast<uint16_t>(42);

        CHECK(FormatTo(out, "{:05}", value) == 5);
        CHECK(out == "00042");
        CHECK_FALSE(errorSeen);
    }

    SECTION("int64_t default decimal") {
        etl::string<64> out;
        int64_t value = INT64_C(1234567890123456789);

        CHECK(FormatTo(out, "{}", value) == 19);
        CHECK(out == "1234567890123456789");
        CHECK_FALSE(errorSeen);
    }

    SECTION("int64_t negative decimal") {
        etl::string<64> out;
        int64_t value = -INT64_C(1234567890123456789);

        CHECK(FormatTo(out, "{}", value) == 20);
        CHECK(out == "-1234567890123456789");
        CHECK_FALSE(errorSeen);
    }

    SECTION("int64_t minimum value") {
        etl::string<64> out;
        int64_t value = std::numeric_limits<int64_t>::min();

        CHECK(FormatTo(out, "{}", value) == 20);
        CHECK(std::string(out.data(), out.size()) == std::to_string(value));
        CHECK_FALSE(errorSeen);
    }

    SECTION("int64_t maximum value") {
        etl::string<64> out;
        int64_t value = std::numeric_limits<int64_t>::max();

        CHECK(FormatTo(out, "{}", value) == 19);
        CHECK(std::string(out.data(), out.size()) == std::to_string(value));
        CHECK_FALSE(errorSeen);
    }

    SECTION("int64_t hexadecimal lowercase") {
        etl::string<64> out;
        int64_t value = INT64_C(0x1234ABCDEF);

        CHECK(FormatTo(out, "{:x}", value) == 10);
        CHECK(out == "1234abcdef");
        CHECK_FALSE(errorSeen);
    }

    SECTION("int64_t alternate uppercase hexadecimal") {
        etl::string<64> out;
        int64_t value = INT64_C(0x1234ABCDEF);

        CHECK(FormatTo(out, "{:#X}", value) == 12);
        CHECK(out == "0X1234ABCDEF");
        CHECK_FALSE(errorSeen);
    }

    SECTION("int64_t width right align") {
        etl::string<64> out;
        int64_t value = INT64_C(42);

        CHECK(FormatTo(out, "{:8}", value) == 8);
        CHECK(out == "      42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("int64_t zero padded with sign") {
        etl::string<64> out;
        int64_t value = INT64_C(42);

        CHECK(FormatTo(out, "{:+08}", value) == 8);
        CHECK(out == "+0000042");
        CHECK_FALSE(errorSeen);
    }

    SECTION("uint64_t default decimal") {
        etl::string<64> out;
        uint64_t value = std::numeric_limits<uint64_t>::max();

        CHECK(FormatTo(out, "{}", value) == 20);
        CHECK(out == "18446744073709551615");
        CHECK_FALSE(errorSeen);
    }

    SECTION("uint64_t hexadecimal lowercase") {
        etl::string<64> out;
        uint64_t value = UINT64_C(0xFFFFFFFFFFFFFFFF);

        CHECK(FormatTo(out, "{:x}", value) == 16);
        CHECK(out == "ffffffffffffffff");
        CHECK_FALSE(errorSeen);
    }

    SECTION("uint64_t hexadecimal uppercase alternate") {
        etl::string<64> out;
        uint64_t value = UINT64_C(0xFFFFFFFFFFFFFFFF);

        CHECK(FormatTo(out, "{:#X}", value) == 18);
        CHECK(out == "0XFFFFFFFFFFFFFFFF");
        CHECK_FALSE(errorSeen);
    }

    SECTION("uint64_t binary formatting") {
        etl::string<128> out;
        uint64_t value = UINT64_C(1) << 63;

        CHECK(FormatTo(out, "{:b}", value) == 64);
        CHECK(out == "1000000000000000000000000000000000000000000000000000000000000000");
        CHECK_FALSE(errorSeen);
    }

    SECTION("uint64_t octal formatting") {
        etl::string<64> out;
        uint64_t value = UINT64_C(511);

        CHECK(FormatTo(out, "{:o}", value) == 3);
        CHECK(out == "777");
        CHECK_FALSE(errorSeen);
    }

    SECTION("uint64_t dynamic width") {
        etl::string<64> out;
        uint64_t value = UINT64_C(42);

        CHECK(FormatTo(out, "{:>{}}", value, 6) == 6);
        CHECK(out == "    42");
        CHECK_FALSE(errorSeen);
    }
}


TEST_CASE("FormatTo string formatting") {
    using namespace pulse::fmt;
    errorSeen = false;

    SECTION("C string default formatting") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{}", "hello") == 5);
        CHECK(out == "hello");
        CHECK_FALSE(errorSeen);
    }

    SECTION("etl::string_view default formatting") {
        etl::string<64> out;
        etl::string_view value = "world";

        CHECK(FormatTo(out, "{}", value) == 5);
        CHECK(out == "world");
        CHECK_FALSE(errorSeen);
    }

    SECTION("etl::string default formatting") {
        etl::string<32> value("example");
        etl::string<64> out;

        CHECK(FormatTo(out, "{}", value) == 7);
        CHECK(out == "example");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Empty string") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{}", "") == 0);
        CHECK(out == "");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Literal text around string") {
        etl::string<64> out;

        CHECK(FormatTo(out, "Value: {}", "test") == 11);
        CHECK(out == "Value: test");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Multiple strings") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{} {} {}", "one", "two", "three") == 13);
        CHECK(out == "one two three");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Repeated argument") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{0}-{0}-{0}", "x") == 5);
        CHECK(out == "x-x-x");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Argument reordering") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{1} {0}", "first", "second") == 12);
        CHECK(out == "second first");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Right aligned width") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:>8}", "cat") == 8);
        CHECK(out == "     cat");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Left aligned width") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:<8}", "cat") == 8);
        CHECK(out == "cat     ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Left aligned width (default)") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:8}", "cat") == 8);
        CHECK(out == "cat     ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Center aligned width") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:^7}", "cat") == 7);
        CHECK(out == "  cat  ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Center aligned width (odd)") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:^8}", "cat") == 8);
        CHECK(out == "  cat   ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Custom fill right align") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:_>8}", "cat") == 8);
        CHECK(out == "_____cat");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Custom fill left align") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:_<8}", "cat") == 8);
        CHECK(out == "cat_____");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Custom fill center align") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:_^7}", "cat") == 7);
        CHECK(out == "__cat__");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Precision truncation") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:.3}", "abcdef") == 3);
        CHECK(out == "abc");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Precision exact size") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:.6}", "abcdef") == 6);
        CHECK(out == "abcdef");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Precision larger than string") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:.10}", "abc") == 3);
        CHECK(out == "abc");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Width and precision combined") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:>8.3}", "abcdef") == 8);
        CHECK(out == "     abc");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Left align with width and precision") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:<8.3}", "abcdef") == 8);
        CHECK(out == "abc     ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Center align with width and precision") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:^7.3}", "abcdef") == 7);
        CHECK(out == "  abc  ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic width") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:>{}}", "cat", 6) == 6);
        CHECK(out == "   cat");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic precision") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:.{}}", "abcdef", 4) == 4);
        CHECK(out == "abcd");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic width and precision") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:>{}.{}}", "abcdef", 8, 3) == 8);
        CHECK(out == "     abc");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Zero width") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:0}", "abc") == 3);
        CHECK(out == "abc");
        CHECK_FALSE(errorSeen);
    }

    SECTION("String type specifier") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:s}", "hello") == 5);
        CHECK(out == "hello");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Escaped braces around string") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{{{}}}", "abc") == 5);
        CHECK(out == "{abc}");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Embedded nulls in string_view") {
        etl::string<64> out;
        const char raw[] = {'a', 'b', '\0', 'c', 'd'};
        etl::string_view value(raw, 5);

        CHECK(FormatTo(out, "{}", value) == 5);
        CHECK(out.size() == 5);
        CHECK(out[0] == 'a');
        CHECK(out[1] == 'b');
        CHECK(out[2] == '\0');
        CHECK(out[3] == 'c');
        CHECK(out[4] == 'd');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Buffer truncation with small output buffer") {
        etl::string<4> out;

        size_t written = FormatTo(out, "{}", "abcdef");
        CHECK(written <= out.max_size());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Mixed string and integer formatting") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{} = {}", "value", 42) == 10);
        CHECK(out == "value = 42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Whitespace string preserved") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{}", "  abc  ") == 7);
        CHECK(out == "  abc  ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic width with explicit argument index") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{0:>{1}}", "cat", 6) == 6);
        CHECK(out == "   cat");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic width with reordered explicit argument index") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{1:>{0}}", 6, "cat") == 6);
        CHECK(out == "   cat");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic precision with explicit argument index") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{0:.{1}}", "abcdef", 4) == 4);
        CHECK(out == "abcd");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic precision with reordered explicit argument index") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{1:.{0}}", 4, "abcdef") == 4);
        CHECK(out == "abcd");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic width and precision with explicit indices") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{0:>{1}.{2}}", "abcdef", 8, 3) == 8);
        CHECK(out == "     abc");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic width and precision with reordered indices") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{2:>{0}.{1}}", 8, 3, "abcdef") == 8);
        CHECK(out == "     abc");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic width and precision both referencing same argument") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{0:>{1}.{1}}", "abcdef", 4) == 4);
        CHECK(out == "abcd");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic width from third argument") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{0:>{2}}", "xy", 999, 5) == 5);
        CHECK(out == "   xy");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic precision from third argument") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{0:.{2}}", "abcdef", 999, 2) == 2);
        CHECK(out == "ab");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic width and precision independent reordered") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{1:{2}.{0}}", 3, "abcdef", 8) == 8);
        CHECK(out == "abc     ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Nested explicit width with center alignment") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{0:^{1}}", "cat", 7) == 7);
        CHECK(out == "  cat  ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Nested explicit precision with center alignment") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{0:^7.{1}}", "abcdef", 3) == 7);
        CHECK(out == "  abc  ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Explicit width zero") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{0:{1}}", "abc", 0) == 3);
        CHECK(out == "abc");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Explicit precision zero") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{0:.{1}}", "abc", 0) == 0);
        CHECK(out == "");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Bad type") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:z}", 0) == 0);
        CHECK(out == "");
        CHECK(errorSeen);
    }
}


TEST_CASE("FormatTo pointer formatting") {
    using namespace pulse::fmt;
    errorSeen = false;

    SECTION("Null pointer default formatting") {
        etl::string<64> out;
        void* ptr = nullptr;

        size_t written = FormatTo(out, "{}", ptr);

        CHECK(written == out.size());
        CHECK_FALSE(out.empty());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Null pointer explicit pointer type") {
        etl::string<64> out;
        void* ptr = nullptr;

        size_t written = FormatTo(out, "{:p}", ptr);

        CHECK(written == out.size());
        CHECK_FALSE(out.empty());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Object pointer formatting") {
        etl::string<64> out;
        int value = 42;
        int* ptr = &value;

        size_t written = FormatTo(out, "{}", ptr);

        CHECK(written == out.size());
        CHECK_FALSE(out.empty());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Object pointer explicit type") {
        etl::string<64> out;
        int value = 42;
        int* ptr = &value;

        size_t written = FormatTo(out, "{:p}", ptr);

        CHECK(written == out.size());
        CHECK_FALSE(out.empty());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Const pointer formatting") {
        etl::string<64> out;
        const int value = 123;
        const int* ptr = &value;

        size_t written = FormatTo(out, "{:p}", ptr);

        CHECK(written == out.size());
        CHECK_FALSE(out.empty());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Pointer should contain hexadecimal digits") {
        etl::string<64> out;
        int value = 1;
        int* ptr = &value;

        FormatTo(out, "{:p}", ptr);

        CHECK_FALSE(out.empty());

        CHECK(out.starts_with("0x"));
        for (char c : out) {
            bool isValid = (c >= '0' && c <= '9') ||
                (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F') ||
                c == 'x' || c == 'X';
            CHECK(isValid);
        }
        CHECK_FALSE(errorSeen);
    }

    SECTION("Pointer width right align") {
        etl::string<128> base;
        etl::string<128> padded;
        int value = 42;
        int* ptr = &value;

        FormatTo(base, "{:p}", ptr);
        CHECK(FormatTo(padded, "{:>20p}", ptr) == 20);

        REQUIRE(padded.size() == 20);
        CHECK(padded.substr(20 - base.size()) == base);
        CHECK_FALSE(errorSeen);
    }

    SECTION("Pointer width left align") {
        etl::string<128> base;
        etl::string<128> padded;
        int value = 42;
        int* ptr = &value;

        FormatTo(base, "{:p}", ptr);
        CHECK(FormatTo(padded, "{:<20p}", ptr) == 20);

        REQUIRE(padded.size() == 20);
        CHECK(padded.substr(0, base.size()) == base);
        CHECK_FALSE(errorSeen);
    }

    SECTION("Pointer width center align") {
        etl::string<128> out;
        int value = 42;
        int* ptr = &value;

        CHECK(FormatTo(out, "{:^20p}", ptr) == 20);
        CHECK(out.size() == 20);
        CHECK_FALSE(errorSeen);
    }

    SECTION("Pointer custom fill") {
        etl::string<128> base;
        etl::string<128> out;
        int value = 42;
        int* ptr = &value;

        FormatTo(base, "{:p}", ptr);
        CHECK(FormatTo(out, "{:_>20p}", ptr) == 20);

        REQUIRE(out.size() == 20);
        CHECK(out.substr(20 - base.size()) == base);

        for (size_t i = 0; i < 20 - base.size(); ++i) {
            CHECK(out[i] == '_');
        }
        CHECK_FALSE(errorSeen);
    }

    SECTION("Repeated pointer argument") {
        etl::string<256> out;
        int value = 42;
        int* ptr = &value;

        size_t written = FormatTo(out, "{0:p} {0:p}", ptr);

        CHECK(written == out.size());

        auto spacePos = out.find(' ');
        REQUIRE(spacePos != etl::string<256>::npos);

        CHECK(out.substr(0, spacePos) == out.substr(spacePos + 1));
        CHECK_FALSE(errorSeen);
    }

    SECTION("Pointer argument reordering") {
        etl::string<256> out;
        int a = 1;
        int b = 2;

        FormatTo(out, "{1:p} {0:p}", &a, &b);

        auto spacePos = out.find(' ');
        REQUIRE(spacePos != etl::string<256>::npos);

        CHECK(out.substr(0, spacePos) != out.substr(spacePos + 1));
        CHECK_FALSE(errorSeen);
    }

    SECTION("Literal braces around pointer") {
        etl::string<128> out;
        int value = 42;

        size_t n = FormatTo(out, "{{{:p}}}", &value);
        CHECK(n == out.size());
        CHECK(out.front() == '{');
        CHECK(out.back() == '}');
        CHECK_FALSE(errorSeen);
    }

    SECTION("Void pointer formatting") {
        etl::string<64> out;
        int value = 42;
        void* ptr = static_cast<void*>(&value);

        size_t written = FormatTo(out, "{:p}", ptr);

        CHECK(written == out.size());
        CHECK_FALSE(out.empty());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Function pointer formatting if supported") {
        etl::string<128> out;

        auto fn = +[]() {};

        size_t written = FormatTo(out, "{}", reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(fn)));

        CHECK(written == out.size());
        CHECK_FALSE(out.empty());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Small buffer truncation") {
        etl::string<4> out;
        int value = 42;

        size_t written = FormatTo(out, "{:p}", &value);

        CHECK(written <= out.max_size());
        CHECK_FALSE(errorSeen);
    }
}


TEST_CASE("FormatTo CustomType formatting") {
    using namespace pulse::fmt;
    errorSeen = false;

    SECTION("Lowercase custom type formatting") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{:z}", value) == 9);
        CHECK(out == "custom_42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Uppercase custom type formatting") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{:Z}", value) == 9);
        CHECK(out == "CUSTOM_42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Zero value lowercase") {
        etl::string<64> out;
        CustomType value(0);

        CHECK(FormatTo(out, "{:z}", value) == 8);
        CHECK(out == "custom_0");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Negative value lowercase") {
        etl::string<64> out;
        CustomType value(-42);

        CHECK(FormatTo(out, "{:z}", value) == 10);
        CHECK(out == "custom_-42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Negative value uppercase") {
        etl::string<64> out;
        CustomType value(-42);

        CHECK(FormatTo(out, "{:Z}", value) == 10);
        CHECK(out == "CUSTOM_-42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Width default alignment") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{:12z}", value) == 12);
        CHECK(out == "custom_42   ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Left alignment") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{:<12z}", value) == 12);
        CHECK(out == "custom_42   ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Right alignment") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{:>12z}", value) == 12);
        CHECK(out == "   custom_42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Center alignment") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{:^13z}", value) == 13);
        CHECK(out == "  custom_42  ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Custom fill left alignment") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{:_<12z}", value) == 12);
        CHECK(out == "custom_42___");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Custom fill right alignment") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{:_>12z}", value) == 12);
        CHECK(out == "___custom_42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Custom fill center alignment") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{:_^13z}", value) == 13);
        CHECK(out == "__custom_42__");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Width equal to formatted size") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{:9z}", value) == 9);
        CHECK(out == "custom_42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Width smaller than content") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{:4z}", value) == 9);
        CHECK(out == "custom_42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Multiple custom values") {
        etl::string<128> out;

        CHECK(FormatTo(out, "{:z} {:Z}", CustomType(1), CustomType(2)) == 17);
        CHECK(out == "custom_1 CUSTOM_2");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Repeated argument") {
        etl::string<128> out;
        CustomType value(7);

        CHECK(FormatTo(out, "{0:z}-{0:Z}", value) == 17);
        CHECK(out == "custom_7-CUSTOM_7");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Argument reordering") {
        etl::string<128> out;

        CHECK(FormatTo(out, "{1:Z} {0:z}", CustomType(10), CustomType(20)) == 19);
        CHECK(out == "CUSTOM_20 custom_10");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Mixed with integer") {
        etl::string<128> out;

        CHECK(FormatTo(out, "{} {:z}", 123, CustomType(42)) == 13);
        CHECK(out == "123 custom_42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Mixed with string") {
        etl::string<128> out;

        CHECK(FormatTo(out, "{} {:Z}", "value:", CustomType(42)) == 16);
        CHECK(out == "value: CUSTOM_42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Literal braces around custom type") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{{{:z}}}", CustomType(42)) == 11);
        CHECK(out == "{custom_42}");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Large integer payload") {
        etl::string<64> out;
        CustomType value(123456789);

        CHECK(FormatTo(out, "{:z}", value) == 16);
        CHECK(out == "custom_123456789");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Bad type specifier should fail") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{:x}", value) == 0);
        CHECK(errorSeen);
    }

    SECTION("Missing type specifier should fail") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{}", value) == 0);
        CHECK(errorSeen);
    }

    SECTION("Unsupported standard string type should fail") {
        etl::string<64> out;
        CustomType value(42);

        CHECK(FormatTo(out, "{:s}", value) == 0);
        CHECK(errorSeen);
    }

    SECTION("Small buffer truncation") {
        etl::string<4> out;

        size_t written = FormatTo(out, "{:z}", CustomType(42));
        CHECK(written <= out.max_size());
        CHECK_FALSE(errorSeen);
    }
}


TEST_CASE("FormatTo floating point formatting") {
    using namespace pulse::fmt;
    errorSeen = false;

    SECTION("Default formatting double") {
        etl::string<64> out;

        size_t written = FormatTo(out, "{}", 3.5);

        CHECK(written == out.size());
        CHECK_FALSE(out.empty());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Default formatting float") {
        etl::string<64> out;
        float value = 1.25f;

        size_t written = FormatTo(out, "{}", value);

        CHECK(written == out.size());
        CHECK_FALSE(out.empty());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Fixed precision") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:.2f}", 3.14159) == 4);
        CHECK(out == "3.14");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Fixed precision rounding") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:.2f}", 3.146) == 4);
        CHECK(out == "3.15");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Fixed precision zero") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:.0f}", 3.9) == 1);
        CHECK(out == "4");
        CHECK_FALSE(errorSeen);
    }

    // SECTION("Scientific lowercase") {
    //     etl::string<64> out;

    //     size_t written = FormatTo(out, "{:.2e}", 1234.0);

    //     CHECK(written == out.size());
    //     CHECK(out.find('e') != etl::string<64>::npos);
    // }

    // SECTION("Scientific uppercase") {
    //     etl::string<64> out;

    //     size_t written = FormatTo(out, "{:.2E}", 1234.0);

    //     CHECK(written == out.size());
    //     CHECK(out.find('E') != etl::string<64>::npos);
    // }

    SECTION("General lowercase") {
        etl::string<64> out;

        size_t written = FormatTo(out, "{:.4g}", 1234.5678);

        CHECK(written == out.size());
        CHECK_FALSE(out.empty());
        CHECK_FALSE(errorSeen);
    }

    SECTION("General uppercase") {
        etl::string<64> out;

        size_t written = FormatTo(out, "{:.4G}", 1234.5678);

        CHECK(written == out.size());
        CHECK_FALSE(out.empty());
        CHECK_FALSE(errorSeen);
    }

    // SECTION("Hex float lowercase") {
    //     etl::string<128> out;

    //     size_t written = FormatTo(out, "{:a}", 3.5);

    //     CHECK(written == out.size());
    //     CHECK_FALSE(out.empty());
    // }

    // SECTION("Hex float uppercase") {
    //     etl::string<128> out;

    //     size_t written = FormatTo(out, "{:A}", 3.5);

    //     CHECK(written == out.size());
    //     CHECK_FALSE(out.empty());
    // }

    SECTION("Explicit positive sign") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:+.2f}", 3.5) == 5);
        CHECK(out == "+3.50");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Negative value") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:.2f}", -3.5) == 5);
        CHECK(out == "-3.50");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Space sign positive") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{: .2f}", 3.5) == 5);
        CHECK(out == " 3.50");
        CHECK_FALSE(errorSeen);
    }

    // SECTION("Alternate form fixed") {
    //     etl::string<64> out;

    //     size_t written = FormatTo(out, "{:#.0f}", 3.0);

    //     CHECK(written == out.size());
    //     CHECK(out.find('.') != etl::string<64>::npos);
    // }

    SECTION("Width right align") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:8.2f}", 3.5) == 8);
        CHECK(out == "    3.50");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Width left align") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:<8.2f}", 3.5) == 8);
        CHECK(out == "3.50    ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Width center align") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:^8.2f}", 3.5) == 8);
        CHECK(out == "  3.50  ");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Custom fill") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:_>8.2f}", 3.5) == 8);
        CHECK(out == "____3.50");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Leading zero padding") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:08.2f}", 3.5) == 8);
        CHECK(out == "00003.50");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Leading zero padding space") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{: 08.2f}", 3.5) == 8);
        CHECK(out == " 0003.50");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Leading zero padding negative") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:08.2f}", -3.5) == 8);
        CHECK(out == "-0003.50");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic width") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:{}.2f}", 3.5, 8) == 8);
        CHECK(out == "    3.50");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic precision") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:.{}f}", 3.14159, 3) == 5);
        CHECK(out == "3.142");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Dynamic width and precision") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:{}.{}f}", 3.14159, 8, 3) == 8);
        CHECK(out == "   3.142");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Explicit indexed dynamic width and precision") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{0:{1}.{2}f}", 3.14159, 8, 2) == 8);
        CHECK(out == "    3.14");
        CHECK_FALSE(errorSeen);
    }

    SECTION("NaN formatting") {
        etl::string<64> out;
        double value = std::numeric_limits<double>::quiet_NaN();

        size_t written = FormatTo(out, "{}", value);

        CHECK(written == out.size());
        CHECK(out == "nan");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Infinity formatting") {
        etl::string<64> out;
        double value = std::numeric_limits<double>::infinity();

        size_t written = FormatTo(out, "{}", value);

        CHECK(written == out.size());
        CHECK(out == "inf");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Negative infinity formatting") {
        etl::string<64> out;
        double value = -std::numeric_limits<double>::infinity();

        size_t written = FormatTo(out, "{}", value);
        CHECK(written == out.size());
        CHECK(out == "-inf");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Zero formatting") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{:.2f}", 0.0) == 4);
        CHECK(out == "0.00");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Negative zero formatting") {
        etl::string<64> out;

        size_t written = FormatTo(out, "{:.2f}", -0.0);

        CHECK(written == out.size());
        bool isValid = out == "-0.00" || out == "0.00";
        CHECK(isValid);
        CHECK_FALSE(errorSeen);
    }

    SECTION("Large double") {
        etl::string<128> out;
        double value = std::numeric_limits<double>::max();

        size_t written = FormatTo(out, "{}", value);

        CHECK(written == out.size());
        CHECK_FALSE(out.empty());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Small double") {
        etl::string<128> out;
        double value = std::numeric_limits<double>::min();

        size_t written = FormatTo(out, "{}", value);

        CHECK(written == out.size());
        CHECK_FALSE(out.empty());
        CHECK_FALSE(errorSeen);
    }

    SECTION("Literal braces") {
        etl::string<64> out;

        CHECK(FormatTo(out, "{{{:.2f}}}", 3.5) == 6);
        CHECK(out == "{3.50}");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Multiple floating point values") {
        etl::string<128> out;

        CHECK(FormatTo(out, "{:.1f} {:.2f}", 1.5, 2.25) == 8);
        CHECK(out == "1.5 2.25");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Mixed float and integer") {
        etl::string<128> out;

        CHECK(FormatTo(out, "{:.2f} {}", 3.5, 42) == 7);
        CHECK(out == "3.50 42");
        CHECK_FALSE(errorSeen);
    }

    SECTION("Small buffer truncation") {
        etl::string<4> out;

        size_t written = FormatTo(out, "{:.2f}", 123.456);

        CHECK(written <= out.max_size());
        CHECK_FALSE(errorSeen);
    }
}
