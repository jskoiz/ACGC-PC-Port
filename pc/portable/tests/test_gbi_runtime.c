#include "acgc/gbi_reference_registry.h"
#include "acgc/gbi_runtime.h"
#include "../../../src/actor/ac_mbg_gbi.h"

#include <libforest/gbi_extensions.h>
#include <PR/mbi.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static Vtx static_reference_vertices[8] = { { 0 } };
static Mtx static_reference_matrix;
static u8 static_reference_texture[128];
static u16 static_reference_palette[16];
static u16 static_reference_color_image[16];
static u16 static_reference_depth_image[16];
static const Gfx static_reference_nested[] = {
    gsSPEndDisplayList(),
};
static const Gfx static_reference_commands[] = {
    gsSPVertex(static_reference_vertices, 8, 0),
    gsSPMatrix(&static_reference_matrix, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH),
    gsDPSetTextureImage_Dolphin(G_IM_FMT_CI, G_IM_SIZ_4b, 16, 32, static_reference_texture),
    gsDPLoadTLUT_Dolphin(15, 16, 1, static_reference_palette),
    gsSPDisplayList(static_reference_nested),
    gsSPDisplayList(SEGMENT_ADDR(G_MWO_SEGMENT_A, 0)),
    gsSPEndDisplayList(),
};
static const Gfx static_reference_noop[] = {
    gsSPNoOp(),
    gsSPEndDisplayList(),
};
static const Gfx static_reference_standard_images[] = {
    gsDPSetColorImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, static_reference_color_image),
    gsDPSetDepthImage(static_reference_depth_image),
    gsDPSetTextureImage(G_IM_FMT_CI, G_IM_SIZ_8b, 4, static_reference_texture),
    gsSPEndDisplayList(),
};

/*
 * The first live graph prefix is a branch-list command, not a draw. Its w1
 * value is the LP64 runtime handle for the target list; the target's ENDDL is
 * outside the copied eight-word prefix and is therefore required evidence for
 * a complete guest submission.
 */
static int test_live_branch_prefix_guest_semantics(void) {
    Gfx target[1] = { { 0 } };
    Gfx branch[1] = { { 0 } };
    uintptr_t resolved = 0;
    AcgcGbiRuntimePtrStatus status;

    gSPEndDisplayList(target);
    gSPBranchList(branch, target);

    CHECK(branch[0].words.w0 == UINT32_C(0xDE010000));
    CHECK(target[0].words.w0 == UINT32_C(0xDF000000));
    CHECK(target[0].words.w1 == 0);

#if UINTPTR_MAX > UINT32_MAX
    /* The first registration after the test-process registry init is the
       exact F0002000 handle observed in the live graph prefix. */
    CHECK(branch[0].words.w1 == UINT32_C(0xF0002000));
    status = pc_gbi_unpack_runtime_ptr(branch[0].words.w1, &resolved);
    CHECK(status == ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(resolved == (uintptr_t)target);
#else
    status = pc_gbi_unpack_runtime_ptr(branch[0].words.w1, &resolved);
    CHECK(status != ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
#endif
    return 0;
}

static const Gfx* static_reference_at(size_t logical_index) {
    return static_reference_commands +
           logical_index * ACGC_GBI_STATIC_REFERENCE_PHYSICAL_WIDTH;
}

static int check_static_reference(
    const Gfx* command,
    u8 expected_command,
    uintptr_t expected_value,
    int expected_is_pointer
) {
    uintptr_t resolved = 0;
    int is_pointer = 0;

    CHECK(ACGC_GBI_STATIC_REFERENCE_IS_WELL_FORMED(command->words.w1));
    CHECK(ACGC_GBI_STATIC_REFERENCE_COMMAND(command->words.w1) == expected_command);
    CHECK(pc_gbi_unpack_static_reference(
        command->words.w1,
        (command + 1)->static_reference,
        (command + 2)->words.w0,
        (command + 2)->words.w1,
        &resolved,
        &is_pointer
    ) == ACGC_GBI_STATIC_REFERENCE_RESOLVED);
    CHECK(resolved == expected_value);
    CHECK(is_pointer == expected_is_pointer);
    return 0;
}

static int test_static_reference_layout(void) {
    const u32 expected_vtx_w0 =
        _SHIFTL(G_VTX, 24, 8) |
        _SHIFTL(8, 12, 8) |
        _SHIFTL(8, 1, 7);
    const u32 expected_mtx_w0 =
        _SHIFTL(G_MTX, 24, 8) |
        _SHIFTL((sizeof(Mtx) - 1) / 8, 19, 5) |
        _SHIFTL((G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH) ^ G_MTX_PUSH, 0, 8);
    const u32 expected_timg_w0 =
        _SHIFTL(G_SETTIMG, 24, 8) |
        _SHIFTL(G_IM_FMT_CI, 21, 3) |
        _SHIFTL(G_IM_SIZ_4b, 19, 2) |
        _SHIFTL(1, 18, 1) |
        _SHIFTL((32 / 4) - 1, 10, 8) |
        _SHIFTL(16 - 1, 0, 10);
    const u32 expected_tlut_w0 =
        _SHIFTL(G_LOADTLUT, 24, 8) |
        _SHIFTL(G_TLUT_DOLPHIN, 22, 2) |
        _SHIFTL(15, 16, 4) |
        _SHIFTL(1, 14, 2) |
        _SHIFTL(16, 0, 14);

    CHECK(sizeof(Gfx) == 8);
#if UINTPTR_MAX > UINT32_MAX
    CHECK(sizeof(static_reference_commands) ==
          (6 * ACGC_GBI_STATIC_REFERENCE_PHYSICAL_WIDTH + 1) * sizeof(Gfx));
    CHECK((uintptr_t)static_reference_vertices > (uintptr_t)UINT32_MAX);
    CHECK((uintptr_t)&static_reference_matrix > (uintptr_t)UINT32_MAX);
    CHECK((uintptr_t)static_reference_texture > (uintptr_t)UINT32_MAX);
    CHECK((uintptr_t)static_reference_palette > (uintptr_t)UINT32_MAX);
    CHECK((uintptr_t)static_reference_nested > (uintptr_t)UINT32_MAX);
#else
    CHECK(sizeof(static_reference_commands) ==
          (6 * ACGC_GBI_STATIC_REFERENCE_PHYSICAL_WIDTH + 1) * sizeof(Gfx));
#endif

    CHECK(static_reference_at(0)->words.w0 == expected_vtx_w0);
    CHECK(static_reference_at(1)->words.w0 == expected_mtx_w0);
    CHECK(static_reference_at(2)->words.w0 == expected_timg_w0);
    CHECK(static_reference_at(3)->words.w0 == expected_tlut_w0);
    CHECK(static_reference_at(4)->words.w0 == _SHIFTL(G_DL, 24, 8));
    CHECK(static_reference_at(5)->words.w0 == _SHIFTL(G_DL, 24, 8));
    CHECK(static_reference_at(6)->words.w0 == _SHIFTL(G_ENDDL, 24, 8));
    CHECK(static_reference_noop[0].words.w0 == _SHIFTL(G_SPNOOP, 24, 8));
    CHECK(static_reference_noop[1].words.w0 == _SHIFTL(G_ENDDL, 24, 8));
    CHECK(sizeof(static_reference_noop) == 2 * sizeof(Gfx));
    CHECK(sizeof(static_reference_standard_images) ==
          (3 * ACGC_GBI_STATIC_REFERENCE_PHYSICAL_WIDTH + 1) * sizeof(Gfx));

#if UINTPTR_MAX > UINT32_MAX
    CHECK(check_static_reference(
        static_reference_at(0),
        G_VTX,
        (uintptr_t)static_reference_vertices,
        1
    ) == 0);
    CHECK(check_static_reference(
        static_reference_at(1),
        G_MTX,
        (uintptr_t)&static_reference_matrix,
        1
    ) == 0);
    CHECK(check_static_reference(
        static_reference_at(2),
        G_SETTIMG,
        (uintptr_t)static_reference_texture,
        1
    ) == 0);
    CHECK(check_static_reference(
        static_reference_at(3),
        G_LOADTLUT,
        (uintptr_t)static_reference_palette,
        1
    ) == 0);
    CHECK(check_static_reference(
        static_reference_at(4),
        G_DL,
        (uintptr_t)static_reference_nested,
        1
    ) == 0);
    CHECK(check_static_reference(
        static_reference_at(5),
        G_DL,
        (uintptr_t)SEGMENT_ADDR(G_MWO_SEGMENT_A, 0),
        0
    ) == 0);
    CHECK(static_reference_at(5)[1].static_reference ==
          ACGC_GBI_STATIC_REFERENCE_PACK_RAW(SEGMENT_ADDR(G_MWO_SEGMENT_A, 0)));
    CHECK(ACGC_GBI_STATIC_REFERENCE_TRAILER_IS_VALID(
        static_reference_at(0)[2].words.w0,
        static_reference_at(0)[2].words.w1));
    CHECK(ACGC_GBI_STATIC_REFERENCE_TAG(
        (uintptr_t)static_reference_vertices, 1, G_VTX
    ) == ACGC_GBI_STATIC_REFERENCE_TAG(
        (uintptr_t)static_reference_palette, 1, G_VTX
    ));
#else
    CHECK(static_reference_at(0)->words.w1 == (u32)static_reference_vertices);
    CHECK(static_reference_at(2)->words.w1 == (u32)static_reference_texture);
    CHECK(static_reference_at(5)->words.w1 ==
          (u32)SEGMENT_ADDR(G_MWO_SEGMENT_A, 0));
#endif
#if UINTPTR_MAX > UINT32_MAX
    CHECK(check_static_reference(
        static_reference_standard_images + 0,
        G_SETCIMG,
        (uintptr_t)static_reference_color_image,
        1
    ) == 0);
    CHECK(check_static_reference(
        static_reference_standard_images + ACGC_GBI_STATIC_REFERENCE_PHYSICAL_WIDTH,
        G_SETZIMG,
        (uintptr_t)static_reference_depth_image,
        1
    ) == 0);
    CHECK(check_static_reference(
        static_reference_standard_images + 2 * ACGC_GBI_STATIC_REFERENCE_PHYSICAL_WIDTH,
        G_SETTIMG,
        (uintptr_t)static_reference_texture,
        1
    ) == 0);
#else
    CHECK(static_reference_standard_images[0].words.w1 ==
          (u32)static_reference_color_image);
    CHECK(static_reference_standard_images[1].words.w1 ==
          (u32)static_reference_depth_image);
    CHECK(static_reference_standard_images[2].words.w1 ==
          (u32)static_reference_texture);
#endif
    return 0;
}

static int test_static_reference_fail_closed(void) {
    const u32 malformed_tag =
        ACGC_GBI_STATIC_REFERENCE_PREFIX |
        _SHIFTL(G_RDPHALF_1, ACGC_GBI_STATIC_REFERENCE_COMMAND_SHIFT, 8) |
        UINT32_C(1);
    const u32 raw_tag = ACGC_GBI_STATIC_REFERENCE_TAG(0, 0, G_DL);
    uintptr_t resolved = UINTPTR_MAX;
    int is_pointer = 1;

    CHECK(ACGC_GBI_STATIC_REFERENCE_IS_TAGGED(malformed_tag));
    CHECK(!ACGC_GBI_STATIC_REFERENCE_IS_WELL_FORMED(malformed_tag));
    CHECK(pc_gbi_unpack_static_reference(
        malformed_tag,
        (uintptr_t)UINT32_C(0xCAFEBABE),
        0,
        0,
        &resolved,
        &is_pointer
    ) == ACGC_GBI_STATIC_REFERENCE_INVALID_REFERENCE);
    CHECK(resolved == 0);
    CHECK(is_pointer == 0);
#if UINTPTR_MAX > UINT32_MAX
    const uintptr_t marked_raw =
        ACGC_GBI_STATIC_REFERENCE_PACK_RAW(UINT32_C(0xCAFEBABE));

    CHECK(ACGC_GBI_STATIC_REFERENCE_IS_RAW_PAYLOAD(marked_raw));
    CHECK(pc_gbi_unpack_static_reference(
        raw_tag,
        marked_raw,
        ACGC_GBI_STATIC_REFERENCE_TRAILER_W0,
        ACGC_GBI_STATIC_REFERENCE_TRAILER_W1,
        &resolved,
        &is_pointer
    ) == ACGC_GBI_STATIC_REFERENCE_RESOLVED);
    CHECK(resolved == (uintptr_t)UINT32_C(0xCAFEBABE));
    CHECK(is_pointer == 0);
    CHECK(!ACGC_GBI_STATIC_REFERENCE_IS_RAW_PAYLOAD((uintptr_t)UINT32_MAX + (uintptr_t)1));
    CHECK(pc_gbi_unpack_static_reference(
        raw_tag,
        (uintptr_t)UINT32_MAX + (uintptr_t)1,
        ACGC_GBI_STATIC_REFERENCE_TRAILER_W0,
        ACGC_GBI_STATIC_REFERENCE_TRAILER_W1,
        &resolved,
        &is_pointer
    ) == ACGC_GBI_STATIC_REFERENCE_INVALID_REFERENCE);
    CHECK(resolved == 0);
    CHECK(is_pointer == 0);
#endif
    return 0;
}

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

static int test_segment_address_pointer_cast_stays_guest_word(void) {
    uintptr_t resolved = UINTPTR_MAX;
    uint32_t packed;

    packed = pc_gbi_pack_runtime_ptr(
        (uintptr_t)UINT32_C(0x0D000080),
        1,
        "segmented matrix pointer cast",
        __FILE__,
        __LINE__
    );

    CHECK(packed == UINT32_C(0x0D000080));
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

static int test_runtime_mbg_model(void) {
    static const Gfx expected[ACGC_MBG_MODEL_GFX_COUNT] = {
        gsDPPipeSync(),
        gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2),
        gsDPSetCombineLERP(PRIMITIVE, 0, SHADE, 0, 0, 0, 0, 1, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED),
        gsDPSetPrimColor(0, 128, 255, 255, 0, 255),
        gsSPLoadGeometryMode(G_ZBUFFER | G_SHADE | G_FOG | G_LIGHTING | G_SHADING_SMOOTH),
        {{
            _SHIFTL(G_VTX, 24, 8) | _SHIFTL(8, 12, 8) | _SHIFTL(8, 1, 7),
            0,
        }},
        gsSP2Triangles(5, 6, 7, 0, 4, 5, 7, 0),
        gsSP2Triangles(7, 6, 2, 0, 7, 2, 3, 0),
        gsSP2Triangles(5, 1, 6, 0, 6, 1, 2, 0),
        gsSP2Triangles(4, 0, 5, 0, 5, 0, 1, 0),
        gsSP2Triangles(4, 7, 0, 0, 0, 7, 3, 0),
        gsSPEndDisplayList(),
    };
    Gfx model[ACGC_MBG_MODEL_GFX_COUNT] = { { 0 } };
    Gfx submit[1] = { { 0 } };
    Vtx vertices[8] = { { 0 } };
    uintptr_t resolved = 0;
    uint32_t old_vertex_handle;
    uint32_t old_model_handle;

    CHECK(sizeof(Gfx) == 8);
#if UINTPTR_MAX > UINT32_MAX
    CHECK((uintptr_t)&vertices[0] > (uintptr_t)UINT32_MAX);
#endif

    pc_gbi_reset_runtime_ptr_registry();
    ac_mbg_build_model(model, &vertices[0]);
    gSPDisplayList(submit + 0, model);

    for (int command = 0; command < ACGC_MBG_MODEL_GFX_COUNT; command++) {
        CHECK(model[command].words.w0 == expected[command].words.w0);
        if (command != 5) {
            CHECK(model[command].words.w1 == expected[command].words.w1);
        }
    }

    old_vertex_handle = model[5].words.w1;
    old_model_handle = submit[0].words.w1;
#if UINTPTR_MAX > UINT32_MAX
    CHECK((old_vertex_handle & ACGC_GBI_REFERENCE_HANDLE_PREFIX_MASK) ==
          ACGC_GBI_REFERENCE_HANDLE_PREFIX);
    CHECK((old_model_handle & ACGC_GBI_REFERENCE_HANDLE_PREFIX_MASK) ==
          ACGC_GBI_REFERENCE_HANDLE_PREFIX);
    CHECK(pc_gbi_unpack_runtime_ptr(old_vertex_handle, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(resolved == (uintptr_t)&vertices[0]);
    CHECK(pc_gbi_unpack_runtime_ptr(old_model_handle, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(resolved == (uintptr_t)model);
#else
    CHECK(pc_gbi_unpack_runtime_ptr(old_vertex_handle, &resolved) !=
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(pc_gbi_unpack_runtime_ptr(old_model_handle, &resolved) !=
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
#endif

    pc_gbi_reset_runtime_ptr_registry();
#if UINTPTR_MAX > UINT32_MAX
    CHECK(pc_gbi_unpack_runtime_ptr(old_vertex_handle, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(resolved == 0);
    CHECK(pc_gbi_unpack_runtime_ptr(old_model_handle, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(resolved == 0);
#else
    CHECK(pc_gbi_unpack_runtime_ptr(old_vertex_handle, &resolved) !=
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(pc_gbi_unpack_runtime_ptr(old_model_handle, &resolved) !=
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
#endif

    ac_mbg_build_model(model, &vertices[0]);
    gSPDisplayList(submit + 0, model);
    for (int command = 0; command < ACGC_MBG_MODEL_GFX_COUNT; command++) {
        CHECK(model[command].words.w0 == expected[command].words.w0);
        if (command != 5) {
            CHECK(model[command].words.w1 == expected[command].words.w1);
        }
    }
#if UINTPTR_MAX > UINT32_MAX
    CHECK(model[5].words.w1 != old_vertex_handle);
    CHECK(submit[0].words.w1 != old_model_handle);
    CHECK(pc_gbi_unpack_runtime_ptr(model[5].words.w1, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(resolved == (uintptr_t)&vertices[0]);
    CHECK(pc_gbi_unpack_runtime_ptr(submit[0].words.w1, &resolved) ==
          ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(resolved == (uintptr_t)model);
#else
    CHECK(pc_gbi_unpack_runtime_ptr(model[5].words.w1, &resolved) !=
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(pc_gbi_unpack_runtime_ptr(submit[0].words.w1, &resolved) !=
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

/* Static LP64 pointer commands occupy three physical Gfx entries, while the
   runtime builder deliberately keeps one entry per logical command. This
   iterator makes the comparison valid on both layouts and on ILP32. */
static size_t static_gfx_command_width(const Gfx* command, size_t remaining) {
#if defined(TARGET_PC) && UINTPTR_MAX > UINT32_MAX
    uintptr_t resolved = 0;
    int is_pointer = 0;

    if (remaining >= ACGC_GBI_STATIC_REFERENCE_PHYSICAL_WIDTH &&
        ACGC_GBI_STATIC_REFERENCE_IS_WELL_FORMED(command[0].words.w1) &&
        ACGC_GBI_STATIC_REFERENCE_COMMAND(command[0].words.w1) == (u8)command[0].dma.cmd &&
        pc_gbi_unpack_static_reference(
            command[0].words.w1,
            command[1].static_reference,
            command[2].words.w0,
            command[2].words.w1,
            &resolved,
            &is_pointer
        ) == ACGC_GBI_STATIC_REFERENCE_RESOLVED) {
        (void)resolved;
        (void)is_pointer;
        return ACGC_GBI_STATIC_REFERENCE_PHYSICAL_WIDTH;
    }
#else
    (void)command;
    (void)remaining;
#endif
    return 1;
}

static int compare_logical_command_words(
    const Gfx* expected,
    size_t expected_count,
    const Gfx* actual,
    size_t actual_count
) {
    size_t expected_index = 0;
    size_t actual_index = 0;

    while (expected_index < expected_count && actual_index < actual_count) {
        if (expected[expected_index].words.w0 != actual[actual_index].words.w0) {
            return 1;
        }
        size_t expected_width = static_gfx_command_width(
            expected + expected_index,
            expected_count - expected_index
        );

#if defined(TARGET_PC) && UINTPTR_MAX > UINT32_MAX
        if (expected_width == ACGC_GBI_STATIC_REFERENCE_PHYSICAL_WIDTH) {
            uintptr_t expected_value = 0;
            uintptr_t actual_value = 0;
            int expected_is_pointer = 0;
            AcgcGbiStaticReferenceStatus static_status;

            static_status = pc_gbi_unpack_static_reference(
                expected[expected_index].words.w1,
                expected[expected_index + 1].static_reference,
                expected[expected_index + 2].words.w0,
                expected[expected_index + 2].words.w1,
                &expected_value,
                &expected_is_pointer
            );
            if (static_status != ACGC_GBI_STATIC_REFERENCE_RESOLVED) {
                return 1;
            }
            if (expected_is_pointer) {
                AcgcGbiRuntimePtrStatus runtime_status =
                    pc_gbi_unpack_runtime_ptr(
                        actual[actual_index].words.w1,
                        &actual_value
                    );
                if (runtime_status == ACGC_GBI_RUNTIME_PTR_NOT_REFERENCE) {
                    actual_value = (uintptr_t)actual[actual_index].words.w1;
                } else if (runtime_status != ACGC_GBI_RUNTIME_PTR_RESOLVED) {
                    return 1;
                }
                if (actual_value != expected_value) {
                    return 1;
                }
            } else if (actual[actual_index].words.w1 != (u32)expected_value) {
                return 1;
            }
        } else if (expected[expected_index].words.w1 != actual[actual_index].words.w1) {
            return 1;
        }
#else
        if (expected[expected_index].words.w1 != actual[actual_index].words.w1) {
            return 1;
        }
#endif

        expected_index += expected_width;
        actual_index++;
    }

    return expected_index == expected_count && actual_index == actual_count ? 0 : 1;
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
        size_t expected_tail_count = idx == 0 ?
            sizeof(type0_tail) / sizeof(type0_tail[0]) :
            sizeof(type1_tail) / sizeof(type1_tail[0]);
        size_t actual_tail_count = idx == 0 ? 5 : 4;

        build_runtime_mailbox_flag_model(common, model[idx], idx);
        gSPDisplayList(submit[idx] + 0, model[idx]);
        gSPEndDisplayList(submit[idx] + 1);

        CHECK(model[idx][0].words.w0 == _SHIFTL(G_DL, 24, 8));
        CHECK(submit[idx][0].words.w0 == _SHIFTL(G_DL, 24, 8));
        CHECK(model[idx][2].words.w1 == (u32)SEGMENT_ADDR(idx == 0 ? ANIME_1_TXT_SEG : ANIME_2_TXT_SEG, 0));
        CHECK(pc_gbi_unpack_runtime_ptr(model[idx][2].words.w1, &resolved) ==
              ACGC_GBI_RUNTIME_PTR_NOT_REFERENCE);
        CHECK(model[idx][3].words.w1 == 0);
        CHECK(compare_logical_command_words(
            expected_tail,
            expected_tail_count,
            model[idx] + 1,
            actual_tail_count
        ) == 0);

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
    CHECK(test_live_branch_prefix_guest_semantics() == 0);
    CHECK(test_static_reference_layout() == 0);
    CHECK(test_static_reference_fail_closed() == 0);
    CHECK(test_direct_tag_and_normal_path() == 0);
    CHECK(test_segment_address_pointer_cast_stays_guest_word() == 0);
    CHECK(test_registry_pointer_round_trip() == 0);
    CHECK(test_high_pointer_round_trip() == 0);
    CHECK(test_reserved_references_fail_closed() == 0);
    CHECK(test_reset_after_consumption_invalidates_handles() == 0);
    CHECK(test_runtime_display_list_commands() == 0);
    CHECK(test_runtime_mbg_model() == 0);
    CHECK(test_runtime_mailbox_flag_models() == 0);
    CHECK(test_runtime_tlut_commands() == 0);
    printf("acgc GBI runtime tests passed\n");
    return 0;
}
