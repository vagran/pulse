#include <catch2/catch_test_macros.hpp>
#include <pulse/format.h>

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

} // anonymous namespace


TEST_CASE("FormatSpec::Parse") {

    fmt::FormatSpec spec;

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
    }

    SECTION("Argument index only") {
        REQUIRE(spec.Parse("2"));

        CHECK(spec.argId == 2);
    }

    SECTION("Width only") {
        REQUIRE(spec.Parse(":10"));

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 10);
    }

    SECTION("Left alignment") {
        REQUIRE(spec.Parse(":<10"));

        CHECK(spec.align == '<');
        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 10);
    }

    SECTION("Right alignment with fill") {
        REQUIRE(spec.Parse(":_>8"));

        CHECK(spec.fill == '_');
        CHECK(spec.align == '>');
        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 8);
    }

    SECTION("Center alignment with fill") {
        REQUIRE(spec.Parse(":*^12"));

        CHECK(spec.fill == '*');
        CHECK(spec.align == '^');
        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 12);
    }

    SECTION("Sign plus") {
        REQUIRE(spec.Parse(":+"));

        CHECK(spec.sign == '+');
    }

    SECTION("Sign minus") {
        REQUIRE(spec.Parse(":-"));

        CHECK(spec.sign == '-');
    }

    SECTION("Sign space") {
        REQUIRE(spec.Parse(": "));

        CHECK(spec.sign == ' ');
    }

    SECTION("Alternate form") {
        REQUIRE(spec.Parse(":#x"));

        CHECK(spec.alternate);
        CHECK(spec.type == 'x');
    }

    SECTION("Leading zeros") {
        REQUIRE(spec.Parse(":08d"));

        CHECK(spec.leadingZeros);
        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 8);
        CHECK(spec.type == 'd');
    }

    SECTION("Precision only") {
        REQUIRE(spec.Parse(":.5f"));

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == 5);
        CHECK(spec.type == 'f');
    }

    SECTION("Width and precision") {
        REQUIRE(spec.Parse(":10.3f"));

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 10);

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == 3);

        CHECK(spec.type == 'f');
    }

    SECTION("Locale specific") {
        REQUIRE(spec.Parse(":L"));

        CHECK(spec.localeSpecific);
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
    }

    SECTION("Reject invalid alignment") {
        CHECK_FALSE(spec.Parse(":?10"));
    }

    SECTION("Reject invalid type") {
        CHECK_FALSE(spec.Parse(":10!"));
    }

    SECTION("Reject malformed precision") {
        CHECK_FALSE(spec.Parse(":.f"));
    }

    SECTION("Reject duplicate precision") {
        CHECK_FALSE(spec.Parse(":10.2.3f"));
    }

    SECTION("Reject random garbage") {
        CHECK_FALSE(spec.Parse("abc$%^"));
    }

    SECTION("Width from argument reference") {
        REQUIRE(spec.Parse(":{3}"));

        REQUIRE(spec.width.has_value());
        // Negative values encode replacement field index (-2 based).
        CHECK(*spec.width == -5); // arg 3 => -(3 + 2)
        CHECK_FALSE(spec.precision.has_value());
    }

    SECTION("Width from automatic argument reference") {
        REQUIRE(spec.Parse(":{}"));

        REQUIRE(spec.width.has_value());
        // Automatic argument index = -1
        CHECK(*spec.width == -1);
        CHECK_FALSE(spec.precision.has_value());
    }

    SECTION("Precision from argument reference") {
        REQUIRE(spec.Parse(":.{2}f"));

        CHECK_FALSE(spec.width.has_value());

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == -4); // arg 2 => -(2 + 2)

        CHECK(spec.type == 'f');
    }

    SECTION("Precision from automatic argument reference") {
        REQUIRE(spec.Parse(":.{}f"));

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == -1);

        CHECK(spec.type == 'f');
    }

    SECTION("Width and precision both from argument references") {
        REQUIRE(spec.Parse(":{1}.{2}f"));

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == -3); // arg 1 => -(1 + 2)

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == -4); // arg 2 => -(2 + 2)

        CHECK(spec.type == 'f');
    }

    SECTION("Mixed numeric width and argument precision") {
        REQUIRE(spec.Parse(":10.{4}g"));

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 10);

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == -6); // arg 4 => -(4 + 2)

        CHECK(spec.type == 'g');
    }

    SECTION("Mixed argument width and numeric precision") {
        REQUIRE(spec.Parse(":{2}.5f"));

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == -4); // arg 2 => -(2 + 2)

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == 5);

        CHECK(spec.type == 'f');
    }

    SECTION("Reject malformed width argument reference") {
        CHECK_FALSE(spec.Parse(":{abc}"));
    }

    SECTION("Reject malformed precision argument reference") {
        CHECK_FALSE(spec.Parse(":.{x}f"));
    }

    SECTION("Reject missing closing brace in width argument reference") {
        CHECK_FALSE(spec.Parse(":{2"));
    }

    SECTION("Reject missing closing brace in precision argument reference") {
        CHECK_FALSE(spec.Parse(":.{3f"));
    }

    SECTION("Reject empty manual width reference") {
        CHECK_FALSE(spec.Parse(":{ }"));
    }

    SECTION("Dynamic width from explicit zero argument") {
        REQUIRE(spec.Parse(":{0}"));

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == -2); // arg 0 => -(0 + 2)
        CHECK_FALSE(spec.precision.has_value());
    }

    SECTION("Dynamic precision from automatic argument") {
        REQUIRE(spec.Parse(":.{}"));

        REQUIRE(spec.precision.has_value());
        CHECK(*spec.precision == -1);
        CHECK_FALSE(spec.width.has_value());
    }

    SECTION("Alternate hexadecimal with zero padding") {
        REQUIRE(spec.Parse(":#08x"));

        CHECK(spec.alternate);
        CHECK(spec.leadingZeros);

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 8);

        CHECK(spec.type == 'x');
    }

    SECTION("Custom fill with left alignment") {
        REQUIRE(spec.Parse(":*<10"));

        CHECK(spec.fill == '*');
        CHECK(spec.align == '<');

        REQUIRE(spec.width.has_value());
        CHECK(*spec.width == 10);
    }

    SECTION("Character type") {
        REQUIRE(spec.Parse(":c"));

        CHECK(spec.type == 'c');
    }

    SECTION("Reject incomplete width field reference") {
        CHECK_FALSE(spec.Parse(":{"));
    }

    SECTION("Reject stray closing brace") {
        CHECK_FALSE(spec.Parse(":}"));
    }

    SECTION("Reject malformed nested braces") {
        CHECK_FALSE(spec.Parse(":{{}"));
    }

    SECTION("Reject malformed precision closing brace") {
        CHECK_FALSE(spec.Parse(":.}"));
    }

    SECTION("Reject duplicate locale specifier") {
        CHECK_FALSE(spec.Parse(":LxL"));
    }

    SECTION("Reject overflowing field reference index") {
        CHECK_FALSE(spec.Parse(":{999999999999999999}"));
    }

    SECTION("Reject overflowing precision field reference index") {
        CHECK_FALSE(spec.Parse(":.{999999999999999999}f"));
    }
}


TEST_CASE("Basic formatting")
{
    StringOutputStream out;

    SECTION("Basic") {
        REQUIRE(fmt::FormatTo(out, "v1 {} v2 {} end", 42, "test") == out.size());
        REQUIRE(out.str() == "v1 42 v2 test end");
    }

    SECTION("Strings") {
        etl::string<16> s1("test 1");
        const char *s2 = "test 2";
        REQUIRE(fmt::FormatTo(out, "v1 {} v2 {} end", s1, s2) == out.size());
        REQUIRE(out.str() == "v1 test 1 v2 test 2 end");
    }
}
