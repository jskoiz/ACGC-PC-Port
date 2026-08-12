#include <cstddef>
#include <type_traits>

#include "libultra/libultra.h"

using BcmpSignature = int (*)(const void*, const void*, std::size_t);
using BcopySignature = void (*)(const void*, void*, std::size_t);
using BzeroSignature = void (*)(void*, std::size_t);

static_assert(std::is_same<decltype(&bcmp), BcmpSignature>::value,
              "bcmp must use the libc-compatible PC signature");
static_assert(std::is_same<decltype(&bcopy), BcopySignature>::value,
              "bcopy must use the libc-compatible PC signature");
static_assert(std::is_same<decltype(&bzero), BzeroSignature>::value,
              "bzero must use the libc-compatible PC signature");

int main() {
#ifndef _WIN32
    char source[] = "abc";
    char destination[sizeof(source)];

    bzero(destination, sizeof(destination));
    bcopy(source, destination, sizeof(source));
    return bcmp(source, destination, sizeof(source)) != 0;
#endif
    return 0;
}
