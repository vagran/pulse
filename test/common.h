#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <iostream>
#include <etl/string.h>


void
InitHeap();

bool
IsHeapLocked();

void
CheckInHeap(void *addr, std::size_t size);

std::size_t
GetHeapSize();

inline std::ostream &
operator <<(std::ostream& os, const etl::istring &value)
{
    os << std::string(value.data(), value.size());
    return os;
}

#endif /* COMMON_H */
