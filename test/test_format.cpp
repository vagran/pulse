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

} // anonymous namespace


void
FormatError(const char *msg)
{
    UNSCOPED_INFO(msg);
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

    SECTION("Default decimal formatting") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{}", 42) == 2);
        CHECK(out == "42");
    }

    SECTION("Negative signed integer") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{}", -42) == 3);
        CHECK(out == "-42");
    }

    SECTION("Zero value") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{}", 0) == 1);
        CHECK(out == "0");
    }

    SECTION("Explicit positive sign") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:+}", 42) == 3);
        CHECK(out == "+42");
    }

    SECTION("Space sign for positive") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{: }", 42) == 3);
        CHECK(out == " 42");
    }

    SECTION("Space sign for negative") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{: }", -42) == 3);
        CHECK(out == "-42");
    }

    SECTION("Binary formatting") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:b}", 10) == 4);
        CHECK(out == "1010");
    }

    SECTION("Binary alternate form") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:#b}", 10) == 6);
        CHECK(out == "0b1010");
    }

    SECTION("Upper binary alternate form") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:#B}", 10) == 6);
        CHECK(out == "0B1010");
    }

    SECTION("Octal formatting") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:o}", 10) == 2);
        CHECK(out == "12");
    }

    SECTION("Octal alternate form") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:#o}", 10) == 3);
        CHECK(out == "012");
    }

    SECTION("Hex lowercase") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:x}", 255) == 2);
        CHECK(out == "ff");
    }

    SECTION("Hex uppercase") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:X}", 255) == 2);
        CHECK(out == "FF");
    }

    SECTION("Hex alternate lowercase") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:#x}", 255) == 4);
        CHECK(out == "0xff");
    }

    SECTION("Hex alternate uppercase") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:#X}", 255) == 4);
        CHECK(out == "0XFF");
    }

    SECTION("Width right align default") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:5}", 42) == 5);
        CHECK(out == "   42");
    }

    SECTION("Width left align") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:<5}", 42) == 5);
        CHECK(out == "42   ");
    }

    SECTION("Width center align") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:^5}", 42) == 5);
        CHECK(out == " 42  ");
    }

    SECTION("Custom fill right align") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:_>5}", 42) == 5);
        CHECK(out == "___42");
    }

    SECTION("Custom fill left align") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:_<5}", 42) == 5);
        CHECK(out == "42___");
    }

    SECTION("Custom fill center align") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:_^5}", 42) == 5);
        CHECK(out == "_42__");
    }

    SECTION("Leading zero padding") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:05}", 42) == 5);
        CHECK(out == "00042");
    }

    SECTION("Leading zero padding with sign") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:+05}", 42) == 5);
        CHECK(out == "+0042");
    }

    SECTION("Leading zero padding negative") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:05}", -42) == 5);
        CHECK(out == "-0042");
    }

    SECTION("Leading zero padding with alternate hex") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:#06x}", 255) == 6);
        CHECK(out == "0x00ff");
    }

    SECTION("Dynamic width") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:{}}", 42, 5) == 5);
        CHECK(out == "   42");
    }

    SECTION("Dynamic width left align") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{:<{}}", 42, 5) == 5);
        CHECK(out == "42   ");
    }

    SECTION("Argument reordering") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{1} {0}", 10, 20) == 5);
        CHECK(out == "20 10");
    }

    SECTION("Repeated argument") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{0} {0} {0}", 7) == 5);
        CHECK(out == "7 7 7");
    }

    SECTION("Signed minimum int") {
        etl::string<64> out;
        int value = std::numeric_limits<int>::min();
        FormatTo(out, "{}", value);
        CHECK(std::string(out.data(), out.size()) == std::to_string(value));
    }

    SECTION("Signed maximum int") {
        etl::string<64> out;
        int value = std::numeric_limits<int>::max();
        CHECK(FormatTo(out, "{}", value) == out.size());
        CHECK(std::string(out.data(), out.size()) == std::to_string(value));
    }

    SECTION("Unsigned maximum") {
        etl::string<64> out;
        unsigned value = std::numeric_limits<unsigned>::max();
        CHECK(FormatTo(out, "{}", value) == out.size());
        CHECK(std::string(out.data(), out.size()) == std::to_string(value));
    }

    SECTION("Zero with alternate hex") {
        etl::string<64> out;
        FormatTo(out, "{:#x}", 0);
        CHECK((out == "0" || out == "0x0"));
    }

    SECTION("Zero with alternate binary") {
        etl::string<64> out;
        FormatTo(out, "{:#b}", 0);
        CHECK((out == "0" || out == "0b0"));
    }

    SECTION("Multiple integers in one format string") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{} {} {}", 1, 22, 333) == 8);
        CHECK(out == "1 22 333");
    }

    SECTION("Literal braces escaped") {
        etl::string<64> out;
        CHECK(FormatTo(out, "{{{}}}", 42) == 4);
        CHECK(out == "{42}");
    }

    SECTION("Buffer truncation behavior with small buffer") {
        etl::string<4> out;
        size_t written = FormatTo(out, "{}", 123456);
        CHECK(written <= out.max_size());
        CHECK(std::string(out.data(), out.size()) == "1234");
    }

    SECTION("int16_t default decimal") {
        etl::string<64> out;
        int16_t value = static_cast<int16_t>(12345);

        CHECK(FormatTo(out, "{}", value) == 5);
        CHECK(out == "12345");
    }

    SECTION("int16_t negative value") {
        etl::string<64> out;
        int16_t value = static_cast<int16_t>(-12345);

        CHECK(FormatTo(out, "{}", value) == 6);
        CHECK(out == "-12345");
    }

    SECTION("int16_t minimum value") {
        etl::string<64> out;
        int16_t value = std::numeric_limits<int16_t>::min();

        CHECK(FormatTo(out, "{}", value) == 6);
        CHECK(out == "-32768");
    }

    SECTION("int16_t maximum value") {
        etl::string<64> out;
        int16_t value = std::numeric_limits<int16_t>::max();

        CHECK(FormatTo(out, "{}", value) == 5);
        CHECK(out == "32767");
    }

    SECTION("int16_t hexadecimal alternate") {
        etl::string<64> out;
        int16_t value = static_cast<int16_t>(255);

        CHECK(FormatTo(out, "{:#x}", value) == 4);
        CHECK(out == "0xff");
    }

    SECTION("int16_t binary formatting") {
        etl::string<64> out;
        int16_t value = static_cast<int16_t>(10);

        CHECK(FormatTo(out, "{:b}", value) == 4);
        CHECK(out == "1010");
    }

    SECTION("uint16_t default decimal") {
        etl::string<64> out;
        uint16_t value = static_cast<uint16_t>(65535);

        CHECK(FormatTo(out, "{}", value) == 5);
        CHECK(out == "65535");
    }

    SECTION("uint16_t hexadecimal uppercase") {
        etl::string<64> out;
        uint16_t value = static_cast<uint16_t>(65535);

        CHECK(FormatTo(out, "{:X}", value) == 4);
        CHECK(out == "FFFF");
    }

    SECTION("uint16_t alternate binary") {
        etl::string<64> out;
        uint16_t value = static_cast<uint16_t>(15);

        CHECK(FormatTo(out, "{:#b}", value) == 6);
        CHECK(out == "0b1111");
    }

    SECTION("uint16_t width and zero padding") {
        etl::string<64> out;
        uint16_t value = static_cast<uint16_t>(42);

        CHECK(FormatTo(out, "{:05}", value) == 5);
        CHECK(out == "00042");
    }

    SECTION("int64_t default decimal") {
        etl::string<64> out;
        int64_t value = INT64_C(1234567890123456789);

        CHECK(FormatTo(out, "{}", value) == 19);
        CHECK(out == "1234567890123456789");
    }

    SECTION("int64_t negative decimal") {
        etl::string<64> out;
        int64_t value = -INT64_C(1234567890123456789);

        CHECK(FormatTo(out, "{}", value) == 20);
        CHECK(out == "-1234567890123456789");
    }

    SECTION("int64_t minimum value") {
        etl::string<64> out;
        int64_t value = std::numeric_limits<int64_t>::min();

        CHECK(FormatTo(out, "{}", value) == 20);
        CHECK(std::string(out.data(), out.size()) == std::to_string(value));
    }

    SECTION("int64_t maximum value") {
        etl::string<64> out;
        int64_t value = std::numeric_limits<int64_t>::max();

        CHECK(FormatTo(out, "{}", value) == 19);
        CHECK(std::string(out.data(), out.size()) == std::to_string(value));
    }

    SECTION("int64_t hexadecimal lowercase") {
        etl::string<64> out;
        int64_t value = INT64_C(0x1234ABCDEF);

        CHECK(FormatTo(out, "{:x}", value) == 10);
        CHECK(out == "1234abcdef");
    }

    SECTION("int64_t alternate uppercase hexadecimal") {
        etl::string<64> out;
        int64_t value = INT64_C(0x1234ABCDEF);

        CHECK(FormatTo(out, "{:#X}", value) == 12);
        CHECK(out == "0X1234ABCDEF");
    }

    SECTION("int64_t width right align") {
        etl::string<64> out;
        int64_t value = INT64_C(42);

        CHECK(FormatTo(out, "{:8}", value) == 8);
        CHECK(out == "      42");
    }

    SECTION("int64_t zero padded with sign") {
        etl::string<64> out;
        int64_t value = INT64_C(42);

        CHECK(FormatTo(out, "{:+08}", value) == 8);
        CHECK(out == "+0000042");
    }

    SECTION("uint64_t default decimal") {
        etl::string<64> out;
        uint64_t value = std::numeric_limits<uint64_t>::max();

        CHECK(FormatTo(out, "{}", value) == 20);
        CHECK(out == "18446744073709551615");
    }

    SECTION("uint64_t hexadecimal lowercase") {
        etl::string<64> out;
        uint64_t value = UINT64_C(0xFFFFFFFFFFFFFFFF);

        CHECK(FormatTo(out, "{:x}", value) == 16);
        CHECK(out == "ffffffffffffffff");
    }

    SECTION("uint64_t hexadecimal uppercase alternate") {
        etl::string<64> out;
        uint64_t value = UINT64_C(0xFFFFFFFFFFFFFFFF);

        CHECK(FormatTo(out, "{:#X}", value) == 18);
        CHECK(out == "0XFFFFFFFFFFFFFFFF");
    }

    SECTION("uint64_t binary formatting") {
        etl::string<128> out;
        uint64_t value = UINT64_C(1) << 63;

        CHECK(FormatTo(out, "{:b}", value) == 64);
        CHECK(out == "1000000000000000000000000000000000000000000000000000000000000000");
    }

    SECTION("uint64_t octal formatting") {
        etl::string<64> out;
        uint64_t value = UINT64_C(511);

        CHECK(FormatTo(out, "{:o}", value) == 3);
        CHECK(out == "777");
    }

    SECTION("uint64_t dynamic width") {
        etl::string<64> out;
        uint64_t value = UINT64_C(42);

        CHECK(FormatTo(out, "{:>{}}", value, 6) == 6);
        CHECK(out == "    42");
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
}
