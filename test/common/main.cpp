#include <catch2/catch_session.hpp>
#include <common.h>


int
main(int argc, char *argv[])
{
    InitHeap();
    return Catch::Session().run(argc, argv);
}
