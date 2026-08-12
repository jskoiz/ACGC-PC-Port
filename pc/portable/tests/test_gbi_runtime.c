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

static void build_runtime_mailbox_flag_model(Gfx* common, Gfx* model, int idx) {
    Gfx* texture = model + 2;

    gSPDisplayList(model + 0, common);
    /* gDPSetTextureImage_Dolphin takes height,width; preserve the static 16x32 words. */
    if (idx == 0) {
        gDPSetCombineLERP(model + 1, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, PRIMITIVE, 0, COMBINED, 0, 0, 0, 0,
                          COMBINED);
        gDPSetTextureImage_Dolphin(texture++, G_IM_FMT_CI, G_IM_SIZ_4b, 32, 16, anime_1_txt);
        gDPSetTile_Dolphin(texture++, G_DOLPHIN_TLUT_DEFAULT_MODE, 0, 15, GX_MIRROR, GX_CLAMP, 0, 0);
        model[3].words.w1 = 0;
        gDPSetTileSize(model + 4, 0, 0, 0, 124, 124);
        gSPEndDisplayList(model + 5);
    } else {
        gDPSetCombineLERP(model + 1, TEXEL0, 0, SHADE, 0, 0, 0, 0, TEXEL0, PRIMITIVE, 0, COMBINED, 0, 0, 0, 0,
                          COMBINED);
        gDPSetTextureImage_Dolphin(texture++, G_IM_FMT_CI, G_IM_SIZ_4b, 32, 16, anime_2_txt);
        gDPSetTile_Dolphin(texture++, G_DOLPHIN_TLUT_DEFAULT_MODE, 0, 15, GX_MIRROR, GX_CLAMP, 0, 0);
        model[3].words.w1 = 0;
        gSPEndDisplayList(model + 4);
    }
}

static int test_runtime_mailbox_flag_models(void) {
    Gfx common[] = {
        gsSPTexture(65535, 65535, 0, 0, G_ON),
        gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2),
        gsDPSetPrimColor(0, 128, 255, 255, 255, 255),
        gsSPLoadGeometryMode(G_ZBUFFER | G_SHADE | G_FOG | G_SHADING_SMOOTH),
        gsSPEndDisplayList(),
    };
    const Gfx type0_tail[] = {
        gsDPSetCombineLERP(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, PRIMITIVE, 0, COMBINED, 0, 0, 0, 0, COMBINED),
        gsDPLoadTextureBlock_4b_Dolphin(anime_1_txt, G_IM_FMT_CI, 16, 32, 15, GX_MIRROR, GX_CLAMP, 0, 0),
        gsDPSetTileSize(0, 0, 0, 124, 124),
        gsSPEndDisplayList(),
    };
    const Gfx type1_tail[] = {
        gsDPSetCombineLERP(TEXEL0, 0, SHADE, 0, 0, 0, 0, TEXEL0, PRIMITIVE, 0, COMBINED, 0, 0, 0, 0, COMBINED),
        gsDPLoadTextureBlock_4b_Dolphin(anime_2_txt, G_IM_FMT_CI, 16, 32, 15, GX_MIRROR, GX_CLAMP, 0, 0),
        gsSPEndDisplayList(),
    };
    Gfx model[2][6] = { { 0 } };
    Gfx submit[2][2] = { { 0 } };
    uint32_t old_common_handles[2];
    uint32_t old_model_handles[2];
    uintptr_t resolved = 0;

    CHECK(sizeof(Gfx) == 8);
    pc_gbi_reset_runtime_ptr_registry();

    for (int idx = 0; idx < 2; idx++) {
        const Gfx* expected_tail = idx == 0 ? type0_tail : type1_tail;
        int expected_tail_count = idx == 0 ? 5 : 4;

        build_runtime_mailbox_flag_model(common, model[idx], idx);
        gSPDisplayList(submit[idx] + 0, model[idx]);
        gSPEndDisplayList(submit[idx] + 1);

        CHECK(model[idx][0].words.w0 == _SHIFTL(G_DL, 24, 8));
        CHECK(submit[idx][0].words.w0 == _SHIFTL(G_DL, 24, 8));
        CHECK(model[idx][2].words.w1 == (u32)SEGMENT_ADDR(idx == 0 ? ANIME_1_TXT_SEG : ANIME_2_TXT_SEG, 0));
        CHECK(pc_gbi_unpack_runtime_ptr(model[idx][2].words.w1, &resolved) ==
              ACGC_GBI_RUNTIME_PTR_NOT_REFERENCE);
        CHECK(model[idx][3].words.w1 == 0);
        for (int command = 0; command < expected_tail_count; command++) {
            CHECK(model[idx][command + 1].words.w0 == expected_tail[command].words.w0);
            CHECK(model[idx][command + 1].words.w1 == expected_tail[command].words.w1);
        }

        old_common_handles[idx] = model[idx][0].words.w1;
        old_model_handles[idx] = submit[idx][0].words.w1;
        CHECK(pc_gbi_unpack_runtime_ptr(old_common_handles[idx], &resolved) !=
              ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
        if (pc_gbi_unpack_runtime_ptr(old_common_handles[idx], &resolved) ==
            ACGC_GBI_RUNTIME_PTR_RESOLVED) {
            CHECK(resolved == (uintptr_t)common);
        }
        CHECK(pc_gbi_unpack_runtime_ptr(old_model_handles[idx], &resolved) !=
              ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
        if (pc_gbi_unpack_runtime_ptr(old_model_handles[idx], &resolved) ==
            ACGC_GBI_RUNTIME_PTR_RESOLVED) {
            CHECK(resolved == (uintptr_t)model[idx]);
        }
    }

#if UINTPTR_MAX > UINT32_MAX
    pc_gbi_reset_runtime_ptr_registry();
    for (int idx = 0; idx < 2; idx++) {
        CHECK(pc_gbi_unpack_runtime_ptr(old_common_handles[idx], &resolved) ==
              ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
        CHECK(pc_gbi_unpack_runtime_ptr(old_model_handles[idx], &resolved) ==
              ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    }

    for (int idx = 0; idx < 2; idx++) {
        build_runtime_mailbox_flag_model(common, model[idx], idx);
        gSPDisplayList(submit[idx] + 0, model[idx]);
        gSPEndDisplayList(submit[idx] + 1);
        CHECK(model[idx][0].words.w1 != old_common_handles[idx]);
        CHECK(submit[idx][0].words.w1 != old_model_handles[idx]);
        CHECK(pc_gbi_unpack_runtime_ptr(model[idx][0].words.w1, &resolved) ==
              ACGC_GBI_RUNTIME_PTR_RESOLVED);
        CHECK(resolved == (uintptr_t)common);
        CHECK(pc_gbi_unpack_runtime_ptr(submit[idx][0].words.w1, &resolved) ==
              ACGC_GBI_RUNTIME_PTR_RESOLVED);
        CHECK(resolved == (uintptr_t)model[idx]);
    }
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
    CHECK(test_runtime_mailbox_flag_models() == 0);
    CHECK(test_runtime_tlut_commands() == 0);
    printf("acgc GBI runtime tests passed\n");
    return 0;
}
