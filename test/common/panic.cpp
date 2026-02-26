#include <catch2/catch_test_macros.hpp>


extern "C" void Panic(const char *msg);

void
Panic(const char *msg)
{
    FAIL(msg);
}
