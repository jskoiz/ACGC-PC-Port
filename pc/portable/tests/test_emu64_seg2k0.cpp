#include "acgc/gbi_runtime.h"
#include "acgc/gbi_reference_registry.h"
#include "libforest/emu64/emu64.hpp"

#include <stdint.h>
#include <stdio.h>

extern "C" unsigned int pc_image_base = 0;
extern "C" unsigned int pc_image_end = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int test_native_resolution_and_fail_closed_lifetime(void) {
#if UINTPTR_MAX > UINT32_MAX
    emu64 runtime{};
    const uintptr_t synthetic_address =
        (uintptr_t)UINT32_MAX + (uintptr_t)0x1001;
    const uint32_t packed = pc_gbi_pack_runtime_ptr(
        synthetic_address,
        1,
        "synthetic_address",
        __FILE__,
        __LINE__
    );

    CHECK((packed & ACGC_GBI_REFERENCE_HANDLE_PREFIX_MASK) ==
          ACGC_GBI_REFERENCE_HANDLE_PREFIX);
    CHECK(runtime.seg2k0(packed) == synthetic_address);

    /* The synchronous interpreter resets this registry only after command
       consumption. Once reset, the old command word must fail closed. */
    pc_gbi_reset_runtime_ptr_registry();
    CHECK(runtime.seg2k0(packed) == 0);

    CHECK(runtime.seg2k0(ACGC_GBI_REFERENCE_HANDLE_PREFIX) == 0);
    CHECK(runtime.seg2k0(UINT32_C(0xF0002001)) == 0);
#endif
    return 0;
}

int main(void) {
    pc_gbi_reset_runtime_ptr_registry();
    CHECK(test_native_resolution_and_fail_closed_lifetime() == 0);
    printf("acgc emu64 seg2k0 tests passed\n");
    return 0;
}
