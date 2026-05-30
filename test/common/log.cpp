#include <pulse/config.h>
#include <catch2/catch_test_macros.hpp>
#include <iostream>


namespace {

std::string logBuf;

} // anonymous namespace

void
LogPutChar(char c)
{
    if (c != '\n') {
        logBuf += c;
    } else if (!logBuf.empty()) {
        UNSCOPED_INFO(logBuf);
        std::cout << logBuf << '\n';
        logBuf.clear();
    }
}
