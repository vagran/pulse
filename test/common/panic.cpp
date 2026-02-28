#include <catch2/catch_test_macros.hpp>
#include <pulse_config.h>

void
Panic(const char *msg)
{
    FAIL(msg);
}
