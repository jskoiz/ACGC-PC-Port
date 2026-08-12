#include "acgc/gbi_reference_registry.h"
#include "acgc/gbi_runtime.h"

#include <libforest/gbi_extensions.h>
#include <PR/mbi.h>

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

static int test_runtime_display_list_commands(void) {
    Gfx inner[5];
    Gfx outer[3];
    Vtx vertices[8] = { 0 };
    uintptr_t resolved = 0;
    AcgcGbiRuntimePtrStatus status;

    pc_gbi_reset_runtime_ptr_registry();

    gSPClearGeometryMode(inner + 0, G_FOG | G_LIGHTING);
    gSPVertex(inner + 1, &vertices[0], 8, 0);
    gSPCullDisplayList(inner + 2, 0, 7);
    gSPSetGeometryMode(inner + 3, G_FOG | G_LIGHTING);
    gSPEndDisplayList(inner + 4);

    gSPDisplayList(outer + 0, inner);
    gSPDisplayList(outer + 1, SEGMENT_ADDR(G_MWO_SEGMENT_A, 0));
    gSPEndDisplayList(outer + 2);

    CHECK(sizeof(Gfx) == 8);
    CHECK(inner[0].words.w0 ==
          (_SHIFTL(G_GEOMETRYMODE, 24, 8) |
           _SHIFTL(~(u32)(G_FOG | G_LIGHTING), 0, 24)));
    CHECK(inner[0].words.w1 == 0);
    CHECK(inner[1].words.w0 ==
          (_SHIFTL(G_VTX, 24, 8) |
           _SHIFTL(8, 12, 8) |
           _SHIFTL(8, 1, 7)));
    status = pc_gbi_unpack_runtime_ptr(inner[1].words.w1, &resolved);
#if UINTPTR_MAX > UINT32_MAX
    CHECK(status == ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(resolved == (uintptr_t)&vertices[0]);
#else
    CHECK(status != ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
#endif
    CHECK(inner[2].words.w0 == _SHIFTL(G_CULLDL, 24, 8));
    CHECK(inner[2].words.w1 == _SHIFTL(14, 0, 16));
    CHECK(inner[3].words.w0 ==
          (_SHIFTL(G_GEOMETRYMODE, 24, 8) |
           _SHIFTL(~(u32)0, 0, 24)));
    CHECK(inner[3].words.w1 == (u32)(G_FOG | G_LIGHTING));
    CHECK(inner[4].words.w0 == _SHIFTL(G_ENDDL, 24, 8));
    CHECK(inner[4].words.w1 == 0);

    CHECK(outer[0].words.w0 == _SHIFTL(G_DL, 24, 8));
    status = pc_gbi_unpack_runtime_ptr(outer[0].words.w1, &resolved);
#if UINTPTR_MAX > UINT32_MAX
    CHECK(status == ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(resolved == (uintptr_t)inner);
#else
    CHECK(status != ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
#endif
    CHECK(outer[1].words.w0 == _SHIFTL(G_DL, 24, 8));
    CHECK(outer[1].words.w1 == SEGMENT_ADDR(G_MWO_SEGMENT_A, 0));
    CHECK(pc_gbi_unpack_runtime_ptr(outer[1].words.w1, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_NOT_REFERENCE);
    CHECK(outer[2].words.w0 == _SHIFTL(G_ENDDL, 24, 8));
    CHECK(outer[2].words.w1 == 0);

#if UINTPTR_MAX > UINT32_MAX
    pc_gbi_reset_runtime_ptr_registry();
    CHECK(pc_gbi_unpack_runtime_ptr(inner[1].words.w1, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(pc_gbi_unpack_runtime_ptr(outer[0].words.w1, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
#endif
    return 0;
}

static int test_runtime_tlut_commands(void) {
    Gfx tlut[2];
    u16 palette[16] = { 0 };
    uintptr_t palette_addr = (uintptr_t)&palette[0];
    uintptr_t resolved = 0;
    uint32_t first_word;
    AcgcGbiRuntimePtrStatus first_status;
    AcgcGbiRuntimePtrStatus status;
    const u32 expected_w0 =
        _SHIFTL(G_LOADTLUT, 24, 8) |
        _SHIFTL(G_TLUT_DOLPHIN, 22, 2) |
        _SHIFTL(15, 16, 4) |
        _SHIFTL(1, 14, 2) |
        _SHIFTL(16, 0, 14);

#if UINTPTR_MAX > UINT32_MAX
    /* Native macOS should exercise the opaque path with a real high address. */
    CHECK(palette_addr > (uintptr_t)UINT32_MAX);
#endif

    pc_gbi_reset_runtime_ptr_registry();
    gDPLoadTLUT_Dolphin(tlut + 0, 15, 16, 1, &palette[0]);
    gSPEndDisplayList(tlut + 1);

    CHECK(sizeof(Gfx) == 8);
    CHECK(tlut[0].words.w0 == expected_w0);
    CHECK(tlut[1].words.w0 == _SHIFTL(G_ENDDL, 24, 8));
    CHECK(tlut[1].words.w1 == 0);

    first_word = tlut[0].words.w1;
    first_status = pc_gbi_unpack_runtime_ptr(first_word, &resolved);
#if UINTPTR_MAX > UINT32_MAX
    CHECK(first_status == ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(resolved == palette_addr);
#else
    CHECK(first_status != ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
#endif

    pc_gbi_reset_runtime_ptr_registry();
    status = pc_gbi_unpack_runtime_ptr(first_word, &resolved);
    if (first_status == ACGC_GBI_RUNTIME_PTR_RESOLVED) {
        CHECK(status == ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
        CHECK(resolved == 0);
    } else {
        CHECK(status == ACGC_GBI_RUNTIME_PTR_NOT_REFERENCE);
    }

    gDPLoadTLUT_Dolphin(tlut + 0, 15, 16, 1, &palette[0]);
    gSPEndDisplayList(tlut + 1);
    CHECK(tlut[0].words.w0 == expected_w0);
    status = pc_gbi_unpack_runtime_ptr(tlut[0].words.w1, &resolved);
    CHECK(status == first_status);
    if (status == ACGC_GBI_RUNTIME_PTR_RESOLVED) {
        CHECK(resolved == palette_addr);
        CHECK(tlut[0].words.w1 != first_word);
    } else {
        CHECK(tlut[0].words.w1 == first_word);
    }
    return 0;
}

int main(void) {
    pc_gbi_reset_runtime_ptr_registry();
    CHECK(test_direct_tag_and_normal_path() == 0);
    CHECK(test_registry_pointer_round_trip() == 0);
    CHECK(test_high_pointer_round_trip() == 0);
    CHECK(test_reserved_references_fail_closed() == 0);
    CHECK(test_reset_after_consumption_invalidates_handles() == 0);
    CHECK(test_runtime_display_list_commands() == 0);
    CHECK(test_runtime_tlut_commands() == 0);
    printf("acgc GBI runtime tests passed\n");
    return 0;
}
