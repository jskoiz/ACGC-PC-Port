#include "pc_gx_blend_producer.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void GXSetBlendMode(
    u32 type,
    u32 src,
    u32 dst,
    u32 logic_op
);

/* The focused target links pc_gx.c without the full PC host executable. */
int g_pc_window_w = PC_SCREEN_WIDTH;
int g_pc_window_h = PC_SCREEN_HEIGHT;
int g_pc_widescreen_stretch = 0;

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

static const PCGXRawBlend* raw_blend(void) {
    return pc_gx_raw_blend_shadow_fixture();
}

static void reset_state(void) {
    pc_gx_clear_semantic_packet_handoff();
    memset(&g_gx, 0, sizeof(g_gx));
    glad_glBindVertexArray = fixture_gl_bind_vertex_array;
    glad_glBindBuffer = fixture_gl_bind_buffer;

    /* These host defaults remain useful to the legacy path, but do not
     * establish setter-owned Blend provenance. */
    g_gx.blend_mode = GX_BM_NONE;
    g_gx.blend_src = GX_BL_ONE;
    g_gx.blend_dst = GX_BL_ZERO;
    g_gx.blend_logic_op = GX_LO_CLEAR;
}

static void init_valid_raw_blend(PCGXRawBlend* input) {
    memset(input, 0, sizeof(*input));
    input->value.mode = GX_BM_BLEND;
    input->value.source_factor = GX_BL_SRCALPHA;
    input->value.destination_factor = GX_BL_INVSRCALPHA;
    input->value.logic_op = GX_LO_NOOP;
    input->known = 1;
}

static void init_output_sentinel(
    AcgcGxCanonicalBlendState* output
) {
    memset(output, 0xA5, sizeof(*output));
}

static int output_is_sentinel(
    const AcgcGxCanonicalBlendState* output,
    const AcgcGxCanonicalBlendState* sentinel
) {
    return memcmp(output, sentinel, sizeof(*output)) == 0;
}

static int raw_blend_is_zero(void) {
    PCGXRawBlend zero;

    memset(&zero, 0, sizeof(zero));
    return memcmp(raw_blend(), &zero, sizeof(zero)) == 0;
}

static int expect_raw_blend(
    uint32_t mode,
    uint32_t source_factor,
    uint32_t destination_factor,
    uint32_t logic_op
) {
    const PCGXRawBlend* shadow = raw_blend();

    return shadow->known == 1 &&
        shadow->invalid == 0 &&
        shadow->value.mode == mode &&
        shadow->value.source_factor == source_factor &&
        shadow->value.destination_factor == destination_factor &&
        shadow->value.logic_op == logic_op &&
        shadow->reserved[0] == 0 &&
        shadow->reserved[1] == 0;
}

static int test_initial_unknownness_and_producer_failure(void) {
    PCGXRawBlend input;
    AcgcGxCanonicalBlendState output;
    AcgcGxCanonicalBlendState sentinel;

    reset_state();
    CHECK(raw_blend() == &g_gx.raw_blend);
    CHECK(raw_blend_is_zero());

    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_blend_build_canonical(raw_blend(), &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw_blend(&input);
    init_output_sentinel(&output);
    sentinel = output;
    input.known = 0;
    CHECK(!pc_gx_raw_blend_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw_blend(&input);
    CHECK(!pc_gx_raw_blend_build_canonical(&input, NULL));
    CHECK(!pc_gx_raw_blend_build_canonical(NULL, &output));
    return 0;
}

static int test_all_valid_gx_domains_and_inactive_values(void) {
    AcgcGxCanonicalBlendState output;
    uint32_t mode;
    uint32_t source_factor;
    uint32_t destination_factor;
    uint32_t logic_op;

    reset_state();
    for (mode = ACGC_GX_CANONICAL_BLEND_MODE_MIN;
         mode <= ACGC_GX_CANONICAL_BLEND_MODE_MAX;
         mode++) {
        for (source_factor = ACGC_GX_CANONICAL_BLEND_FACTOR_MIN;
             source_factor <= ACGC_GX_CANONICAL_BLEND_FACTOR_MAX;
             source_factor++) {
            for (destination_factor = ACGC_GX_CANONICAL_BLEND_FACTOR_MIN;
                 destination_factor <= ACGC_GX_CANONICAL_BLEND_FACTOR_MAX;
                 destination_factor++) {
                for (logic_op = ACGC_GX_CANONICAL_BLEND_LOGIC_OP_MIN;
                     logic_op <= ACGC_GX_CANONICAL_BLEND_LOGIC_OP_MAX;
                     logic_op++) {
                    GXSetBlendMode(
                        mode, source_factor, destination_factor, logic_op
                    );
                    CHECK(expect_raw_blend(
                        mode, source_factor, destination_factor, logic_op
                    ));
                    CHECK(pc_gx_raw_blend_build_canonical(raw_blend(), &output));
                    CHECK(output.mode == mode);
                    CHECK(output.source_factor == source_factor);
                    CHECK(output.destination_factor == destination_factor);
                    CHECK(output.logic_op == logic_op);
                    CHECK(acgc_gx_canonical_blend_state_validate(&output));
                }
            }
        }
    }
    return 0;
}

static int test_equal_legacy_values_establish_provenance(void) {
    AcgcGxCanonicalBlendState output;
    uint32_t dirty_before;

    reset_state();
    dirty_before = g_gx.dirty;
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    CHECK(expect_raw_blend(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR));
    CHECK(g_gx.dirty == dirty_before);
    CHECK(pc_gx_raw_blend_build_canonical(raw_blend(), &output));
    CHECK(output.mode == GX_BM_NONE);
    CHECK(output.source_factor == GX_BL_ONE);
    CHECK(output.destination_factor == GX_BL_ZERO);
    CHECK(output.logic_op == GX_LO_CLEAR);
    return 0;
}

static int expect_sticky_invalid_setter_call(
    uint32_t mode,
    uint32_t source_factor,
    uint32_t destination_factor,
    uint32_t logic_op
) {
    PCGXRawBlend before;
    AcgcGxCanonicalBlendState output;
    AcgcGxCanonicalBlendState sentinel;

    reset_state();
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    before = *raw_blend();
    g_gx.dirty = 0;

    /* The legacy fields still receive the caller values, even when the
     * sideband rejects this setter epoch. */
    GXSetBlendMode(mode, source_factor, destination_factor, logic_op);
    CHECK(raw_blend()->invalid == 1);
    CHECK(raw_blend()->known == before.known);
    CHECK(memcmp(&raw_blend()->value, &before.value,
                 sizeof(before.value)) == 0);
    CHECK((g_gx.dirty & PC_GX_DIRTY_BLEND) != 0);

    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_blend_build_canonical(raw_blend(), &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    /* A valid later setter cannot silently repair sticky-invalid provenance,
     * while the legacy OpenGL mirror continues its existing behavior. */
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_XOR);
    CHECK(raw_blend()->invalid == 1);
    CHECK(memcmp(&raw_blend()->value, &before.value,
                 sizeof(before.value)) == 0);
    CHECK(g_gx.blend_mode == GX_BM_BLEND);
    CHECK(g_gx.blend_src == GX_BL_SRCALPHA);
    CHECK(g_gx.blend_dst == GX_BL_INVSRCALPHA);
    CHECK(g_gx.blend_logic_op == GX_LO_XOR);
    return 0;
}

static int test_sticky_invalidity_and_legacy_behavior(void) {
    if (expect_sticky_invalid_setter_call(
            GX_MAX_BLENDMODE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR) != 0 ||
        expect_sticky_invalid_setter_call(
            GX_BM_NONE, GX_BL_INVDSTALPHA + 1, GX_BL_ZERO, GX_LO_CLEAR) != 0 ||
        expect_sticky_invalid_setter_call(
            GX_BM_NONE, GX_BL_ONE, GX_BL_INVDSTALPHA + 1, GX_LO_CLEAR) != 0 ||
        expect_sticky_invalid_setter_call(
            GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_SET + 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_producer_rejects_malformed_provenance_and_domains(void) {
    PCGXRawBlend input;
    AcgcGxCanonicalBlendState output;
    AcgcGxCanonicalBlendState sentinel;
    static const uint32_t invalid_modes[] = {4, UINT32_MAX};
    static const uint32_t invalid_factors[] = {8, UINT32_MAX};
    static const uint32_t invalid_logic_ops[] = {16, UINT32_MAX};
    size_t index;

    init_valid_raw_blend(&input);
    for (index = 0;
         index < sizeof(invalid_modes) / sizeof(invalid_modes[0]);
         index++) {
        input.value.mode = invalid_modes[index];
        init_output_sentinel(&output);
        sentinel = output;
        CHECK(!pc_gx_raw_blend_build_canonical(&input, &output));
        CHECK(output_is_sentinel(&output, &sentinel));
        init_valid_raw_blend(&input);
    }
    for (index = 0;
         index < sizeof(invalid_factors) / sizeof(invalid_factors[0]);
         index++) {
        input.value.source_factor = invalid_factors[index];
        init_output_sentinel(&output);
        sentinel = output;
        CHECK(!pc_gx_raw_blend_build_canonical(&input, &output));
        CHECK(output_is_sentinel(&output, &sentinel));
        init_valid_raw_blend(&input);

        input.value.destination_factor = invalid_factors[index];
        init_output_sentinel(&output);
        sentinel = output;
        CHECK(!pc_gx_raw_blend_build_canonical(&input, &output));
        CHECK(output_is_sentinel(&output, &sentinel));
        init_valid_raw_blend(&input);
    }
    for (index = 0;
         index < sizeof(invalid_logic_ops) / sizeof(invalid_logic_ops[0]);
         index++) {
        input.value.logic_op = invalid_logic_ops[index];
        init_output_sentinel(&output);
        sentinel = output;
        CHECK(!pc_gx_raw_blend_build_canonical(&input, &output));
        CHECK(output_is_sentinel(&output, &sentinel));
        init_valid_raw_blend(&input);
    }

    input.known = 2;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_blend_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw_blend(&input);
    input.invalid = 1;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_blend_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw_blend(&input);
    input.reserved[0] = 1;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_blend_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw_blend(&input);
    input.reserved[1] = 1;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_blend_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));
    return 0;
}

static int test_success_does_not_mutate_input(void) {
    PCGXRawBlend input;
    PCGXRawBlend input_before;
    AcgcGxCanonicalBlendState first;
    AcgcGxCanonicalBlendState second;

    init_valid_raw_blend(&input);
    input.value.mode = GX_BM_NONE;
    input.value.source_factor = GX_BL_DSTALPHA;
    input.value.destination_factor = GX_BL_INVDSTALPHA;
    input.value.logic_op = GX_LO_SET;
    input_before = input;

    CHECK(pc_gx_raw_blend_build_canonical(&input, &first));
    CHECK(memcmp(&input, &input_before, sizeof(input)) == 0);

    memset(&second, 0x5A, sizeof(second));
    CHECK(pc_gx_raw_blend_build_canonical(&input, &second));
    CHECK(memcmp(&first, &second, sizeof(first)) == 0);
    CHECK(first.mode == GX_BM_NONE);
    CHECK(first.source_factor == GX_BL_DSTALPHA);
    CHECK(first.destination_factor == GX_BL_INVDSTALPHA);
    CHECK(first.logic_op == GX_LO_SET);
    return 0;
}

typedef struct {
    int calls;
    int in_begin;
    int current_vertex_idx;
    int pending_verts;
    PCGXRawBlend before;
} BlendFlushObservation;

static void observe_blend_flush(
    void* context,
    const AcgcGxSemanticPacket* packet
) {
    BlendFlushObservation* observation = (BlendFlushObservation*)context;

    (void)packet;
    observation->calls++;
    observation->in_begin = g_gx.in_begin;
    observation->current_vertex_idx = g_gx.current_vertex_idx;
    observation->pending_verts = g_gx.pending_verts;
    observation->before = *raw_blend();
}

static void configure_semantic_vertex_color_state(void) {
    PCGXTevStage* stage;

    g_gx.num_tev_stages = 1;
    g_gx.num_tex_gens = 0;
    g_gx.num_ind_stages = 0;
    g_gx.num_chans = 0;
    g_gx.fog_type = GX_FOG_NONE;
    g_gx.alpha_comp0 = GX_ALWAYS;
    g_gx.alpha_ref0 = 0;
    g_gx.alpha_op = GX_AOP_AND;
    g_gx.alpha_comp1 = GX_ALWAYS;
    g_gx.alpha_ref1 = 0;
    g_gx.current_mtx = 0;

    stage = &g_gx.tev_stages[0];
    stage->color_a = GX_CC_ZERO;
    stage->color_b = GX_CC_ZERO;
    stage->color_c = GX_CC_ZERO;
    stage->color_d = GX_CC_RASC;
    stage->alpha_a = GX_CA_ZERO;
    stage->alpha_b = GX_CA_ZERO;
    stage->alpha_c = GX_CA_ZERO;
    stage->alpha_d = GX_CA_RASA;
    stage->color_op = GX_TEV_ADD;
    stage->color_bias = GX_TB_ZERO;
    stage->color_scale = GX_CS_SCALE_1;
    stage->color_clamp = GX_TRUE;
    stage->color_out = GX_TEVPREV;
    stage->alpha_op = GX_TEV_ADD;
    stage->alpha_bias = GX_TB_ZERO;
    stage->alpha_scale = GX_CS_SCALE_1;
    stage->alpha_clamp = GX_TRUE;
    stage->alpha_out = GX_TEVPREV;
    stage->tex_coord = GX_TEXCOORD_NULL;
    stage->tex_map = GX_TEXMAP_NULL;
    stage->color_chan = GX_COLOR0A0;
    stage->ras_swap = GX_TEV_SWAP0;
    stage->tex_swap = GX_TEV_SWAP0;
}

static void prepare_completed_batch(BlendFlushObservation* observation) {
    memset(observation, 0, sizeof(*observation));
    g_gx.in_begin = 1;
    g_gx.expected_vertex_count = 3;
    g_gx.current_vertex_idx = 3;
    g_gx.pending_verts = 0;
    g_gx.vertex_pending = 0;
    g_gx.current_primitive = GX_TRIANGLES;
    g_gx.pending_prim = GX_TRIANGLES;
    pc_gx_set_semantic_packet_handoff(observe_blend_flush, observation);
}

static int test_flush_precedes_raw_mutation(void) {
    BlendFlushObservation observation;

    reset_state();
    configure_semantic_vertex_color_state();
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    g_gx.dirty = 0;
    prepare_completed_batch(&observation);

    GXSetBlendMode(
        GX_BM_BLEND,
        GX_BL_SRCALPHA,
        GX_BL_INVSRCALPHA,
        GX_LO_NOOP
    );
    pc_gx_clear_semantic_packet_handoff();

    CHECK(observation.calls == 1);
    CHECK(observation.in_begin == 0);
    CHECK(observation.current_vertex_idx == 3);
    CHECK(observation.pending_verts == 0);
    CHECK(observation.before.known == 1);
    CHECK(observation.before.invalid == 0);
    CHECK(observation.before.value.mode == GX_BM_NONE);
    CHECK(observation.before.value.source_factor == GX_BL_ONE);
    CHECK(observation.before.value.destination_factor == GX_BL_ZERO);
    CHECK(observation.before.value.logic_op == GX_LO_CLEAR);
    CHECK(expect_raw_blend(
        GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP
    ));
    CHECK(g_gx.pending_verts == 3);
    CHECK(g_gx.blend_mode == GX_BM_BLEND);
    CHECK(g_gx.blend_src == GX_BL_SRCALPHA);
    CHECK(g_gx.blend_dst == GX_BL_INVSRCALPHA);
    CHECK(g_gx.blend_logic_op == GX_LO_NOOP);
    CHECK((g_gx.dirty & PC_GX_DIRTY_BLEND) != 0);
    return 0;
}

int main(void) {
    if (test_initial_unknownness_and_producer_failure() != 0 ||
        test_all_valid_gx_domains_and_inactive_values() != 0 ||
        test_equal_legacy_values_establish_provenance() != 0 ||
        test_sticky_invalidity_and_legacy_behavior() != 0 ||
        test_producer_rejects_malformed_provenance_and_domains() != 0 ||
        test_success_does_not_mutate_input() != 0 ||
        test_flush_precedes_raw_mutation() != 0) {
        return 1;
    }

    puts("pc GX raw Blend producer fixture: PASS");
    puts("proof boundary: setter-owned CPU Blend provenance, flush ordering, and canonical validation only; no renderer, Metal, device, pixel, or playability claim");
    return 0;
}
