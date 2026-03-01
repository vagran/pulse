#ifndef COMMON_H
#define COMMON_H

#include <cstdint>


void
InitHeap();

bool
IsHeapLocked();

void
CheckInHeap(void *addr, std::size_t size);

std::size_t
GetHeapSize();

#endif /* COMMON_H */
