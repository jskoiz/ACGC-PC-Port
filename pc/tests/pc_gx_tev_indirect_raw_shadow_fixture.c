#include "pc_gx_internal.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void GXSetNumTevStages(u8 nStages);
extern void GXSetTevOp(u32 stage, u32 mode);
extern void GXSetTevColorIn(u32 stage, u32 a, u32 b, u32 c, u32 d);
extern void GXSetTevAlphaIn(u32 stage, u32 a, u32 b, u32 c, u32 d);
extern void GXSetTevColorOp(
    u32 stage,
    u32 op,
    u32 bias,
    u32 scale,
    GXBool clamp,
    u32 out_reg
);
extern void GXSetTevAlphaOp(
    u32 stage,
    u32 op,
    u32 bias,
    u32 scale,
    GXBool clamp,
    u32 out_reg
);
extern void GXSetTevOrder(u32 stage, u32 coord, u32 map, u32 color);
extern void GXSetTevColor(u32 id, u32 color_packed);
extern void GXSetTevColorS10(u32 id, s16 r, s16 g, s16 b, s16 a);
extern void GXSetTevKColor(u32 id, u32 color_packed);
extern void GXSetTevKColorSel(u32 stage, u32 sel);
extern void GXSetTevKAlphaSel(u32 stage, u32 sel);
extern void GXSetTevSwapMode(u32 stage, u32 ras_sel, u32 tex_sel);
extern void GXSetTevSwapModeTable(
    u32 table,
    u32 red,
    u32 green,
    u32 blue,
    u32 alpha
);
extern void GXSetNumIndStages(u8 nStages);
extern void GXSetIndTexMtx(u32 mtx_sel, const void* offset, s8 scale);
extern void GXSetIndTexOrder(u32 ind_stage, u32 tex_coord, u32 tex_map);
extern void GXSetTevIndirect(
    u32 stage,
    u32 ind_stage,
    u32 fmt,
    u32 bias_sel,
    u32 mtx_sel,
    u32 wrap_s,
    u32 wrap_t,
    GXBool add_prev,
    GXBool ind_lod,
    u32 alpha_sel
);
extern void GXSetIndTexCoordScale(
    u32 ind_stage,
    u32 scale_s,
    u32 scale_t
);

/* This fixture links pc_gx.c without the full host executable. */
void pc_gx_tev_seq_reset(void) {
}

static PCGXShaderVariant g_fixture_shader_variant;

PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    return &g_fixture_shader_variant;
}

static void fixture_gl_bind_vertex_array(GLuint array) {
    (void)array;
}

static void fixture_gl_bind_buffer(GLenum target, GLuint buffer) {
    (void)target;
    (void)buffer;
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static const PCGXRawTevIndirect* raw_state(void) {
    return &g_gx.raw_tev_indirect;
}

static void reset_state(void) {
    pc_gx_clear_alpha_flush_fixture_observer();
    memset(&g_gx, 0, sizeof(g_gx));
    glad_glBindVertexArray = fixture_gl_bind_vertex_array;
    glad_glBindBuffer = fixture_gl_bind_buffer;
}

static int raw_is_unknown(void) {
    PCGXRawTevIndirect zero;

    memset(&zero, 0, sizeof(zero));
    return memcmp(raw_state(), &zero, sizeof(zero)) == 0;
}

static int test_initial_unknownness_and_partial_knownness(void) {
    reset_state();
    CHECK(raw_is_unknown());

    GXSetTevColorIn(0, 1, 2, 3, 4);
    CHECK(raw_state()->invalid == 0);
    CHECK(raw_state()->stages[0].known_mask ==
          (PC_GX_RAW_TEV_STAGE_COLOR_A |
           PC_GX_RAW_TEV_STAGE_COLOR_B |
           PC_GX_RAW_TEV_STAGE_COLOR_C |
           PC_GX_RAW_TEV_STAGE_COLOR_D));
    CHECK(raw_state()->stages[0].value.color_a == 1);
    CHECK(raw_state()->stages[0].value.color_d == 4);
    CHECK(raw_state()->stages[1].known_mask == 0);
    CHECK(raw_state()->active_tev_stage_count_known == 0);
    CHECK(raw_state()->active_indirect_stage_count_known == 0);
    return 0;
}

static int test_source_faithful_tev_op_expansion(void) {
    reset_state();
    GXSetTevOp(0, GX_BLEND);
    CHECK(raw_state()->invalid == 0);
    CHECK(raw_state()->stages[0].value.color_a == GX_CC_RASC);
    CHECK(raw_state()->stages[0].value.color_b == GX_CC_ONE);
    CHECK(raw_state()->stages[0].value.color_c == GX_CC_TEXC);
    CHECK(raw_state()->stages[0].value.color_d == GX_CC_ZERO);
    CHECK(raw_state()->stages[0].value.alpha_c == GX_CA_RASA);
    /* The legacy PC expansion remains observable in the host mirror. */
    CHECK(g_gx.tev_stages[0].color_a == GX_CC_ONE);
    CHECK(g_gx.tev_stages[0].color_b == GX_CC_RASC);

    reset_state();
    GXSetTevOp(1, GX_BLEND);
    CHECK(raw_state()->invalid == 0);
    CHECK(raw_state()->stages[1].value.color_a == GX_CC_CPREV);
    CHECK(raw_state()->stages[1].value.color_b == GX_CC_ONE);
    CHECK(raw_state()->stages[1].value.color_c == GX_CC_TEXC);
    CHECK(raw_state()->stages[1].value.color_d == GX_CC_ZERO);
    CHECK(raw_state()->stages[1].value.alpha_c == GX_CA_APREV);
    CHECK(g_gx.tev_stages[1].color_a == GX_CC_ONE);
    CHECK(g_gx.tev_stages[1].color_b == GX_CC_RASC);

    GXSetTevOp(2, GX_PASSCLR);
    CHECK(raw_state()->stages[2].value.color_d == GX_CC_CPREV);
    CHECK(raw_state()->stages[2].value.alpha_d == GX_CA_APREV);
    CHECK(g_gx.tev_stages[2].color_d == GX_CC_RASC);
    CHECK(g_gx.tev_stages[2].alpha_d == GX_CA_RASA);
    return 0;
}

static int test_all_tev_stage_fields_and_tables(void) {
    uint32_t stage;

    reset_state();
    GXSetNumTevStages(16);
    CHECK(raw_state()->active_tev_stage_count_known == 1);
    CHECK(raw_state()->active_tev_stage_count == 16);

    for (stage = 0; stage < ACGC_GX_CANONICAL_TEV_STAGE_COUNT; stage++) {
        GXSetTevColorIn(stage, 1, 2, 3, 4);
        GXSetTevAlphaIn(stage, 1, 2, 3, 4);
        GXSetTevColorOp(stage, GX_TEV_ADD, 2, 3, GX_TRUE, GX_TEVREG1);
        GXSetTevAlphaOp(stage, GX_TEV_SUB, 1, 2, GX_FALSE, GX_TEVREG2);
        GXSetTevOrder(stage, 7, 6, 8);
        GXSetTevKColorSel(stage, 12);
        GXSetTevKAlphaSel(stage, 16);
        GXSetTevSwapMode(stage, 2, 3);
        GXSetTevIndirect(stage, 3, 3, 7, 11, 6, 5, GX_TRUE, GX_TRUE, 3);
    }

    for (stage = 0; stage < ACGC_GX_CANONICAL_TEV_STAGE_COUNT; stage++) {
        const PCGXRawTevStage* raw = &raw_state()->stages[stage];

        CHECK(raw->known_mask == PC_GX_RAW_TEV_STAGE_KNOWN_MASK);
        CHECK(raw->value.color_a == 1);
        CHECK(raw->value.color_d == 4);
        CHECK(raw->value.alpha_a == 1);
        CHECK(raw->value.alpha_d == 4);
        CHECK(raw->value.color_op == GX_TEV_ADD);
        CHECK(raw->value.color_bias == 2);
        CHECK(raw->value.color_scale == 3);
        CHECK(raw->value.color_clamp == GX_TRUE);
        CHECK(raw->value.color_out == GX_TEVREG1);
        CHECK(raw->value.alpha_op == GX_TEV_SUB);
        CHECK(raw->value.alpha_bias == 1);
        CHECK(raw->value.alpha_scale == 2);
        CHECK(raw->value.alpha_clamp == GX_FALSE);
        CHECK(raw->value.alpha_out == GX_TEVREG2);
        CHECK(raw->value.tex_coord == 7);
        CHECK(raw->value.tex_map == 6);
        CHECK(raw->value.color_chan == 8);
        CHECK(raw->value.k_color_sel == 12);
        CHECK(raw->value.k_alpha_sel == 16);
        CHECK(raw->value.ras_swap == 2);
        CHECK(raw->value.tex_swap == 3);
        CHECK(raw->value.ind_stage == 3);
        CHECK(raw->value.ind_format == 3);
        CHECK(raw->value.ind_bias == 7);
        CHECK(raw->value.ind_mtx == 11);
        CHECK(raw->value.ind_wrap_s == 6);
        CHECK(raw->value.ind_wrap_t == 5);
        CHECK(raw->value.ind_add_prev == GX_TRUE);
        CHECK(raw->value.ind_lod == GX_TRUE);
        CHECK(raw->value.ind_alpha == 3);
    }

    GXSetTevSwapModeTable(0, 0, 1, 2, 3);
    GXSetTevSwapModeTable(1, 3, 2, 1, 0);
    GXSetTevSwapModeTable(2, 1, 3, 0, 2);
    GXSetTevSwapModeTable(3, 2, 0, 3, 1);
    CHECK(raw_state()->swap_tables[0].known_mask ==
          PC_GX_RAW_TEV_RECORD_KNOWN_MASK);
    CHECK(raw_state()->swap_tables[0].value.r == 0);
    CHECK(raw_state()->swap_tables[0].value.a == 3);
    CHECK(raw_state()->swap_tables[3].value.r == 2);
    CHECK(raw_state()->swap_tables[3].value.a == 1);
    CHECK(raw_state()->invalid == 0);
    return 0;
}

static int expect_raw_color(
    const PCGXRawTevColor* color,
    const int32_t expected[4],
    PCGXTevRawSource source,
    int valid
) {
    uint32_t index;

    CHECK(color->known_mask == PC_GX_RAW_TEV_COMPONENT_KNOWN_MASK);
    CHECK(color->source == (uint8_t)source);
    CHECK(color->valid == (uint8_t)(valid ? 1 : 0));
    for (index = 0; index < 4; index++) {
        CHECK(color->components[index] == expected[index]);
    }
    return 0;
}

static int test_register_and_konst_provenance(void) {
    static const int32_t prev[4] = {1, 2, 3, 4};
    static const int32_t reg0[4] = {5, 6, 7, 8};
    static const int32_t reg1[4] = {-1024, 1023, 0, -1};
    static const int32_t reg2[4] = {255, 0, 127, 64};
    static const int32_t k0[4] = {0x11, 0x22, 0x33, 0x44};
    static const int32_t k1[4] = {0x55, 0x66, 0x77, 0x88};
    static const int32_t k2[4] = {0x99, 0xAA, 0xBB, 0xCC};
    static const int32_t k3[4] = {0xDD, 0xEE, 0xFF, 0x00};

    reset_state();
    GXSetTevColor(GX_TEVPREV, UINT32_C(0x01020304));
    GXSetTevColor(GX_TEVREG0, UINT32_C(0x08070605));
    GXSetTevColorS10(GX_TEVREG1, -1024, 1023, 0, -1);
    GXSetTevColor(GX_TEVREG2, UINT32_C(0xFF007F40));
    GXSetTevKColor(GX_KCOLOR0, UINT32_C(0x11223344));
    GXSetTevKColor(GX_KCOLOR1, UINT32_C(0x55667788));
    GXSetTevKColor(GX_KCOLOR2, UINT32_C(0x99AABBCC));
    GXSetTevKColor(GX_KCOLOR3, UINT32_C(0xDDEEFF00));

    CHECK(expect_raw_color(
        &raw_state()->registers[GX_TEVPREV], prev,
        PCGX_TEV_RAW_SOURCE_COLOR_U8, 1
    ) == 0);
    CHECK(expect_raw_color(
        &raw_state()->registers[GX_TEVREG0], reg0,
        PCGX_TEV_RAW_SOURCE_COLOR_U8, 1
    ) == 0);
    CHECK(expect_raw_color(
        &raw_state()->registers[GX_TEVREG1], reg1,
        PCGX_TEV_RAW_SOURCE_COLOR_S10, 1
    ) == 0);
    CHECK(expect_raw_color(
        &raw_state()->registers[GX_TEVREG2], reg2,
        PCGX_TEV_RAW_SOURCE_COLOR_U8, 1
    ) == 0);
    CHECK(expect_raw_color(
        &raw_state()->konst[GX_KCOLOR0], k0,
        PCGX_TEV_RAW_SOURCE_KCOLOR_U8, 1
    ) == 0);
    CHECK(expect_raw_color(
        &raw_state()->konst[GX_KCOLOR1], k1,
        PCGX_TEV_RAW_SOURCE_KCOLOR_U8, 1
    ) == 0);
    CHECK(expect_raw_color(
        &raw_state()->konst[GX_KCOLOR2], k2,
        PCGX_TEV_RAW_SOURCE_KCOLOR_U8, 1
    ) == 0);
    CHECK(expect_raw_color(
        &raw_state()->konst[GX_KCOLOR3], k3,
        PCGX_TEV_RAW_SOURCE_KCOLOR_U8, 1
    ) == 0);
    CHECK(raw_state()->invalid == 0);
    return 0;
}

static int test_indirect_order_scale_and_matrix_copy(void) {
    static const float matrix0[2][3] = {
        {0.5f, -0.25f, 0.75f},
        {-0.5f, 0.25f, -0.75f}
    };
    float matrix1[2][3] = {
        {0.125f, 0.25f, 0.375f},
        {0.5f, 0.625f, 0.75f}
    };
    float matrix2[2][3] = {
        {-0.125f, -0.25f, -0.375f},
        {-0.5f, -0.625f, -0.75f}
    };
    uint32_t stage;

    reset_state();
    GXSetNumIndStages(4);
    for (stage = 0; stage < ACGC_GX_CANONICAL_INDIRECT_ORDER_CAPACITY; stage++) {
        GXSetIndTexOrder(stage, stage + 1, 7 - stage);
        GXSetIndTexCoordScale(stage, stage, 8 - stage);
        CHECK(raw_state()->orders[stage].known_mask == UINT32_C(0x0F));
        CHECK(raw_state()->orders[stage].value.tex_coord == stage + 1);
        CHECK(raw_state()->orders[stage].value.tex_map == 7 - stage);
        CHECK(raw_state()->orders[stage].value.scale_s == stage);
        CHECK(raw_state()->orders[stage].value.scale_t == 8 - stage);
    }
    CHECK(raw_state()->active_indirect_stage_count_known == 1);
    CHECK(raw_state()->active_indirect_stage_count == 4);

    GXSetIndTexMtx(GX_ITM_0, matrix0, -17);
    GXSetIndTexMtx(GX_ITM_S1, matrix1, 10);
    GXSetIndTexMtx(GX_ITM_T2, matrix2, 1);
    matrix1[0][0] = 99.0f;
    matrix2[1][2] = 99.0f;

    CHECK(raw_state()->matrices[0].known_mask ==
          PC_GX_RAW_INDIRECT_MATRIX_KNOWN_MASK);
    CHECK(raw_state()->matrices[0].value.s0 == 512);
    CHECK(raw_state()->matrices[0].value.t0 == -512);
    CHECK(raw_state()->matrices[0].value.s1 == -256);
    CHECK(raw_state()->matrices[0].value.t1 == 256);
    CHECK(raw_state()->matrices[0].value.s2 == 768);
    CHECK(raw_state()->matrices[0].value.t2 == -768);
    CHECK(raw_state()->matrices[0].value.encoded_scale == 0);
    CHECK(raw_state()->matrices[1].value.s0 == 128);
    CHECK(raw_state()->matrices[1].value.t2 == 768);
    CHECK(raw_state()->matrices[1].value.encoded_scale == 27);
    CHECK(raw_state()->matrices[2].value.s0 == -128);
    CHECK(raw_state()->matrices[2].value.t2 == -768);
    CHECK(raw_state()->matrices[2].value.encoded_scale == 18);
    CHECK(raw_state()->invalid == 0);
    return 0;
}

static int test_sticky_invalidity_preserves_legacy_mirrors(void) {
    PCGXRawTevIndirect before;

    reset_state();
    GXSetTevColorS10(GX_TEVREG0, -1025, 1024, 0, 0);
    CHECK(raw_state()->invalid == 1);
    CHECK(raw_state()->registers[GX_TEVREG0].valid == 0);
    CHECK(raw_state()->registers[GX_TEVREG0].source ==
          PCGX_TEV_RAW_SOURCE_MALFORMED);
    CHECK(raw_state()->registers[GX_TEVREG0].components[0] == -1025);
    CHECK(raw_state()->registers[GX_TEVREG0].components[1] == 1024);
    before = *raw_state();

    GXSetNumTevStages(16);
    GXSetTevColorIn(0, 1, 2, 3, 4);
    GXSetIndTexOrder(0, 1, 2);
    GXSetIndTexMtx(GX_ITM_0, NULL, 0);
    CHECK(memcmp(raw_state(), &before, sizeof(before)) == 0);

    /* The established legacy mirrors remain independently writable. */
    GXSetTevColor(GX_TEVREG0, UINT32_C(0x01020304));
    CHECK(g_gx.tev_raw_colors[GX_TEVREG0].valid == 1);
    CHECK(g_gx.tev_raw_colors[GX_TEVREG0].source ==
          PCGX_TEV_RAW_SOURCE_COLOR_U8);
    CHECK(raw_state()->invalid == 1);
    return 0;
}

static int test_invalid_register_and_konst_ids_fail_closed(void) {
    reset_state();
    GXSetTevColor(GX_MAX_TEVREG, UINT32_C(0x01020304));
    CHECK(raw_state()->invalid == 1);
    CHECK(g_gx.tev_raw_colors[0].valid == 0);
    CHECK(g_gx.tev_raw_colors[0].source ==
          PCGX_TEV_RAW_SOURCE_UNAVAILABLE);
    CHECK(g_gx.tev_colors[0][0] == 0.0f);

    reset_state();
    GXSetTevColorS10(UINT32_MAX, -1025, 1024, 0, 0);
    CHECK(raw_state()->invalid == 1);
    CHECK(g_gx.tev_raw_colors[0].valid == 0);
    CHECK(g_gx.tev_raw_colors[0].source ==
          PCGX_TEV_RAW_SOURCE_UNAVAILABLE);
    CHECK(g_gx.tev_colors[0][0] == 0.0f);

    reset_state();
    GXSetTevKColor(GX_MAX_KCOLOR, UINT32_C(0x01020304));
    CHECK(raw_state()->invalid == 1);
    CHECK(g_gx.tev_raw_k_colors[0].valid == 0);
    CHECK(g_gx.tev_raw_k_colors[0].source ==
          PCGX_TEV_RAW_SOURCE_UNAVAILABLE);
    CHECK(g_gx.tev_k_colors[0][0] == 0.0f);
    return 0;
}

typedef struct {
    int calls;
    int in_begin;
    int current_vertex_idx;
    PCGXRawTevIndirect before;
} TevIndirectFlushObservation;

static void observe_tev_indirect_flush(void* context) {
    TevIndirectFlushObservation* observation =
        (TevIndirectFlushObservation*)context;

    observation->calls++;
    observation->in_begin = g_gx.in_begin;
    observation->current_vertex_idx = g_gx.current_vertex_idx;
    observation->before = *raw_state();
}

static void prepare_completed_batch(TevIndirectFlushObservation* observation) {
    memset(observation, 0, sizeof(*observation));
    g_gx.in_begin = 1;
    g_gx.expected_vertex_count = 1;
    g_gx.current_vertex_idx = 1;
    g_gx.pending_verts = 0;
    g_gx.vertex_pending = 0;
    g_gx.current_primitive = GX_TRIANGLES;
    g_gx.pending_prim = GX_TRIANGLES;
    pc_gx_set_alpha_flush_fixture_observer(
        observe_tev_indirect_flush,
        observation
    );
}

static int test_flush_precedes_tev_and_indirect_mutation(void) {
    TevIndirectFlushObservation observation;

    reset_state();
    GXSetTevColorIn(0, 1, 1, 1, 1);
    g_gx.dirty = 0;
    prepare_completed_batch(&observation);
    GXSetTevColorIn(0, 2, 2, 2, 2);
    pc_gx_clear_alpha_flush_fixture_observer();
    CHECK(observation.calls == 1);
    CHECK(observation.in_begin == 0);
    CHECK(observation.current_vertex_idx == 1);
    CHECK(observation.before.stages[0].value.color_a == 1);
    CHECK(raw_state()->stages[0].value.color_a == 2);

    reset_state();
    GXSetIndTexOrder(0, 1, 2);
    g_gx.dirty = 0;
    prepare_completed_batch(&observation);
    GXSetIndTexOrder(0, 3, 4);
    pc_gx_clear_alpha_flush_fixture_observer();
    CHECK(observation.calls == 1);
    CHECK(observation.before.orders[0].value.tex_coord == 1);
    CHECK(raw_state()->orders[0].value.tex_coord == 3);
    return 0;
}

int main(void) {
    if (test_initial_unknownness_and_partial_knownness() != 0 ||
        test_source_faithful_tev_op_expansion() != 0 ||
        test_all_tev_stage_fields_and_tables() != 0 ||
        test_register_and_konst_provenance() != 0 ||
        test_indirect_order_scale_and_matrix_copy() != 0 ||
        test_sticky_invalidity_preserves_legacy_mirrors() != 0 ||
        test_invalid_register_and_konst_ids_fail_closed() != 0 ||
        test_flush_precedes_tev_and_indirect_mutation() != 0) {
        return 1;
    }

    puts("pc GX raw TEV/Indirect shadow fixture: PASS");
    puts("proof boundary: setter-owned pointer-free CPU TEV/Indirect provenance and flush ordering only; no canonical packet, renderer, Metal, device, or playability claim");
    return 0;
}
