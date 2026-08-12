#include "acgc/gbi_reference_registry.h"
#include "acgc/gbi_runtime.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int test_direct_tag_and_normal_path(void) {
    uintptr_t resolved = UINTPTR_MAX;
    uint32_t packed = pc_gbi_pack_runtime_ptr(
        (uintptr_t)UINT32_C(0x12345678),
        1,
        "direct",
        __FILE__,
        __LINE__
    );

    CHECK(packed == UINT32_C(0x12345679));
    CHECK(pc_gbi_unpack_runtime_ptr(packed, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_NOT_REFERENCE);
    CHECK(resolved == 0);
    return 0;
}

static int test_registry_pointer_round_trip(void) {
    uintptr_t value = (uintptr_t)UINT32_C(0x12345679);
    uintptr_t resolved = 0;
    uint32_t packed;

    packed = pc_gbi_pack_runtime_ptr(value, 1, "odd", __FILE__, __LINE__);
    CHECK((packed & ACGC_GBI_REFERENCE_HANDLE_PREFIX_MASK) ==
          ACGC_GBI_REFERENCE_HANDLE_PREFIX);
    CHECK(pc_gbi_unpack_runtime_ptr(packed, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(resolved == value);
    return 0;
}

static int test_high_pointer_round_trip(void) {
#if UINTPTR_MAX > UINT32_MAX
    const uintptr_t value = (uintptr_t)UINT32_MAX + (uintptr_t)1;
    uintptr_t resolved = 0;
    uint32_t packed;

    CHECK((value & (uintptr_t)1) == 0);
    packed = pc_gbi_pack_runtime_ptr(value, 1, "high", __FILE__, __LINE__);
    CHECK(packed != UINT32_C(1));
    CHECK((packed & ACGC_GBI_REFERENCE_HANDLE_PREFIX_MASK) ==
          ACGC_GBI_REFERENCE_HANDLE_PREFIX);
    CHECK(pc_gbi_unpack_runtime_ptr(packed, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(resolved == value);
#endif
    return 0;
}

static int test_reserved_references_fail_closed(void) {
    uintptr_t resolved = UINTPTR_MAX;

    pc_gbi_reset_runtime_ptr_registry();
    CHECK(pc_gbi_unpack_runtime_ptr(
        ACGC_GBI_REFERENCE_HANDLE_PREFIX,
        &resolved
    ) == ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(resolved == 0);
    CHECK(pc_gbi_unpack_runtime_ptr(
        UINT32_C(0xF0002001),
        &resolved
    ) == ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(resolved == 0);
    CHECK(pc_gbi_unpack_runtime_ptr(
        ACGC_GBI_REFERENCE_HANDLE_PREFIX,
        NULL
    ) == ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    return 0;
}

static int test_reset_after_consumption_invalidates_handles(void) {
    uintptr_t resolved = 0;
    uint32_t packed = pc_gbi_pack_runtime_ptr(
        (uintptr_t)UINT32_C(0x12345679),
        1,
        "consumed",
        __FILE__,
        __LINE__
    );

    CHECK(pc_gbi_unpack_runtime_ptr(packed, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_RESOLVED);
    pc_gbi_reset_runtime_ptr_registry();
    CHECK(pc_gbi_unpack_runtime_ptr(packed, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(resolved == 0);
    return 0;
}

int main(void) {
    pc_gbi_reset_runtime_ptr_registry();
    CHECK(test_direct_tag_and_normal_path() == 0);
    CHECK(test_registry_pointer_round_trip() == 0);
    CHECK(test_high_pointer_round_trip() == 0);
    CHECK(test_reserved_references_fail_closed() == 0);
    CHECK(test_reset_after_consumption_invalidates_handles() == 0);
    printf("acgc GBI runtime tests passed\n");
    return 0;
}
