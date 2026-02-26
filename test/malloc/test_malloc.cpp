/* This file is compiled with differrent combinations of malloc parameters (alignment, granularity,
 * size word size).
 */

#include <catch2/catch_test_macros.hpp>


TEST_CASE("Sample test") {
    REQUIRE(2 * 2 == 4);
}
