#include <stddef.h>
#include <string.h>

#include "_mem.h"
#include "libultra/libultra.h"

_Static_assert(
    _Generic(&bcmp, int (*)(const void*, const void*, size_t): 1, default: 0),
    "bcmp must use the libc-compatible PC signature");
_Static_assert(
    _Generic(&bcopy, void (*)(const void*, void*, size_t): 1, default: 0),
    "bcopy must use the libc-compatible PC signature");
_Static_assert(
    _Generic(&bzero, void (*)(void*, size_t): 1, default: 0),
    "bzero must use the libc-compatible PC signature");

int main(void) {
#ifndef _WIN32
    char source[] = "abc";
    char destination[sizeof(source)];

    memset(destination, 0, sizeof(destination));
    memcpy(destination, source, sizeof(source));
    if (memcmp(source, destination, sizeof(source)) != 0) {
        return 1;
    }
    bzero(destination, sizeof(destination));
    bcopy(source, destination, sizeof(source));
    return bcmp(source, destination, sizeof(source)) != 0;
#endif
    return 0;
}
