#include <pulse_config.h>
#include <catch2/catch_test_macros.hpp>

namespace {

bool isMallocLocked = false;

}

void
TestMallocLock()
{
    REQUIRE_FALSE(isMallocLocked);
    isMallocLocked = true;
}

void
TestMallocUnlock()
{
    REQUIRE(isMallocLocked);
    isMallocLocked = false;
}
