#include <catch2/catch_session.hpp>
#include <pulse/malloc.h>


namespace {

uint8_t heap[1024 * 1024];

}


int
main(int argc, char *argv[])
{
    pulse_add_heap_region(heap, sizeof(heap));
    return Catch::Session().run(argc, argv);
}
