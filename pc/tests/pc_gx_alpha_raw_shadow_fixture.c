#include "pc_gx_internal.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void GXSetAlphaCompare(
    u32 comp0,
    u8 ref0,
    u32 op,
    u32 comp1,
    u8 ref1
);
extern void GXSetAlphaUpdate(GXBool enable);
extern void GXSetColorUpdate(GXBool enable);
extern void GXSetZCompLoc(GXBool before_tex);

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

static const PCGXRawAlpha* raw_alpha(void) {
    return pc_gx_raw_alpha_shadow_fixture();
}

static void reset_state(void) {
    pc_gx_clear_alpha_flush_fixture_observer();
    memset(&g_gx, 0, sizeof(g_gx));
    glad_glBindVertexArray = fixture_gl_bind_vertex_array;
    glad_glBindBuffer = fixture_gl_bind_buffer;

    /* Host defaults remain useful to legacy rendering, but do not establish
     * setter-owned Alpha provenance. */
    g_gx.alpha_comp0 = GX_ALWAYS;
    g_gx.alpha_comp1 = GX_ALWAYS;
    g_gx.alpha_op = GX_AOP_AND;
    g_gx.color_update_enable = GX_TRUE;
    g_gx.alpha_update_enable = GX_TRUE;
}

static void set_complete_alpha_state(GXBool z_comp_loc) {
    GXSetAlphaCompare(
        GX_GREATER,
        37,
        GX_AOP_OR,
        GX_NEQUAL,
        211
    );
    GXSetColorUpdate(GX_FALSE);
    GXSetAlphaUpdate(GX_TRUE);
    GXSetZCompLoc(z_comp_loc);
}

static int raw_alpha_is_unknown(void) {
    PCGXRawAlpha zero;

    memset(&zero, 0, sizeof(zero));
    return memcmp(raw_alpha(), &zero, sizeof(zero)) == 0;
}

static int expect_complete_raw_alpha(GXBool z_comp_loc) {
    const PCGXRawAlpha* shadow = raw_alpha();

    return shadow->known_mask == PC_GX_RAW_ALPHA_KNOWN_ALL &&
        shadow->invalid == 0 &&
        shadow->value.comp0 == GX_GREATER &&
        shadow->value.ref0 == 37 &&
        shadow->value.op == GX_AOP_OR &&
        shadow->value.comp1 == GX_NEQUAL &&
        shadow->value.ref1 == 211 &&
        shadow->value.color_update_enable == GX_FALSE &&
        shadow->value.alpha_update_enable == GX_TRUE &&
        shadow->value.z_comp_loc_before_tex ==
            (z_comp_loc != GX_FALSE ? 1u : 0u);
}

typedef struct {
    int calls;
    int in_begin;
    int current_vertex_idx;
    int pending_verts;
    PCGXRawAlpha before;
} AlphaFlushObservation;

static void observe_alpha_flush(void* context) {
    AlphaFlushObservation* observation =
        (AlphaFlushObservation*)context;

    observation->calls++;
    observation->in_begin = g_gx.in_begin;
    observation->current_vertex_idx = g_gx.current_vertex_idx;
    observation->pending_verts = g_gx.pending_verts;
    observation->before = *raw_alpha();
}

static void prepare_completed_batch(AlphaFlushObservation* observation) {
    memset(observation, 0, sizeof(*observation));
    g_gx.in_begin = 1;
    g_gx.expected_vertex_count = 1;
    g_gx.current_vertex_idx = 1;
    g_gx.pending_verts = 0;
    g_gx.vertex_pending = 0;
    g_gx.current_primitive = GX_TRIANGLES;
    g_gx.pending_prim = GX_TRIANGLES;
    pc_gx_set_alpha_flush_fixture_observer(
        observe_alpha_flush,
        observation
    );
}

static void finish_observed_batch(void) {
    pc_gx_clear_alpha_flush_fixture_observer();
}

static int test_initial_unknownness_and_fail_closed(void) {
    AcgcGxCanonicalAlphaState state;
    AcgcGxCanonicalAlphaState sentinel;

    reset_state();
    CHECK(raw_alpha() == &g_gx.raw_alpha);
    CHECK(raw_alpha_is_unknown());

    memset(&sentinel, 0xA5, sizeof(sentinel));
    state = sentinel;
    CHECK(!pc_gx_raw_alpha_build_canonical(&state));
    CHECK(memcmp(&state, &sentinel, sizeof(state)) == 0);
    return 0;
}

static int test_complete_state_builds_existing_canonical_alpha(void) {
    AcgcGxCanonicalAlphaState state;

    reset_state();
    set_complete_alpha_state(GX_FALSE);
    CHECK(expect_complete_raw_alpha(GX_FALSE));
    CHECK(pc_gx_raw_alpha_build_canonical(&state));
    CHECK(state.comp0 == GX_GREATER);
    CHECK(state.ref0 == 37);
    CHECK(state.op == GX_AOP_OR);
    CHECK(state.comp1 == GX_NEQUAL);
    CHECK(state.ref1 == 211);
    CHECK(state.color_update_enable == GX_FALSE);
    CHECK(state.alpha_update_enable == GX_TRUE);
    CHECK(state.z_comp_loc_before_tex == GX_FALSE);
    CHECK(acgc_gx_canonical_alpha_state_validate(&state));
    return 0;
}

static int test_equal_legacy_values_establish_provenance(void) {
    AcgcGxCanonicalAlphaState state;
    uint32_t dirty_before;

    reset_state();
    dirty_before = g_gx.dirty;
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_TRUE);
    GXSetZCompLoc(GX_TRUE);
    CHECK(g_gx.dirty == dirty_before);
    CHECK(raw_alpha()->known_mask == PC_GX_RAW_ALPHA_KNOWN_ALL);
    CHECK(pc_gx_raw_alpha_build_canonical(&state));
    CHECK(state.comp0 == GX_ALWAYS);
    CHECK(state.ref0 == 0);
    CHECK(state.op == GX_AOP_AND);
    CHECK(state.comp1 == GX_ALWAYS);
    CHECK(state.ref1 == 0);
    CHECK(state.color_update_enable == GX_TRUE);
    CHECK(state.alpha_update_enable == GX_TRUE);
    CHECK(state.z_comp_loc_before_tex == GX_TRUE);
    return 0;
}

static int test_partial_state_remains_unpublishable(void) {
    AcgcGxCanonicalAlphaState state;

    reset_state();
    GXSetZCompLoc(GX_TRUE);
    CHECK(raw_alpha()->known_mask == PC_GX_RAW_ALPHA_KNOWN_Z_COMP_LOC);
    CHECK(!pc_gx_raw_alpha_build_canonical(&state));

    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    CHECK(!pc_gx_raw_alpha_build_canonical(&state));
    return 0;
}

static int test_out_of_range_compare_enum_is_sticky_and_fail_closed(void) {
    AcgcGxCanonicalAlphaState state;
    PCGXRawAlpha before;

    reset_state();
    set_complete_alpha_state(GX_TRUE);
    before = *raw_alpha();

    /* GXSetAlphaCompare accepts compare values through u32, so this checks a
     * representable out-of-range enum without manufacturing a malformed
     * GXBool call. TARGET_PC GXBool is C bool and is only exercised with the
     * valid GX_FALSE/GX_TRUE values elsewhere in this fixture. */
    GXSetAlphaCompare(8, 0, GX_AOP_AND, GX_ALWAYS, 0);
    CHECK(raw_alpha()->invalid == 1);
    CHECK(memcmp(&raw_alpha()->value, &before.value,
                 sizeof(before.value)) == 0);
    CHECK(!pc_gx_raw_alpha_build_canonical(&state));

    /* A valid later setter cannot silently recover an invalid compare epoch. */
    GXSetZCompLoc(GX_FALSE);
    CHECK(raw_alpha()->invalid == 1);
    CHECK(!pc_gx_raw_alpha_build_canonical(&state));
    return 0;
}

static int test_zcomp_flushes_before_raw_mutation(void) {
    AlphaFlushObservation observation;

    reset_state();
    set_complete_alpha_state(GX_TRUE);
    g_gx.dirty = 0;
    prepare_completed_batch(&observation);
    GXSetZCompLoc(GX_FALSE);
    finish_observed_batch();

    CHECK(observation.calls == 1);
    CHECK(observation.in_begin == 0);
    CHECK(observation.current_vertex_idx == 1);
    CHECK(observation.pending_verts == 0);
    CHECK(observation.before.known_mask == PC_GX_RAW_ALPHA_KNOWN_ALL);
    CHECK(observation.before.invalid == 0);
    CHECK(observation.before.value.z_comp_loc_before_tex == GX_TRUE);
    CHECK(g_gx.pending_verts == 1);
    CHECK(raw_alpha()->value.z_comp_loc_before_tex == GX_FALSE);
    CHECK(raw_alpha()->known_mask == PC_GX_RAW_ALPHA_KNOWN_ALL);
    CHECK(raw_alpha()->invalid == 0);
    return 0;
}

int main(void) {
    if (test_initial_unknownness_and_fail_closed() != 0 ||
        test_complete_state_builds_existing_canonical_alpha() != 0 ||
        test_equal_legacy_values_establish_provenance() != 0 ||
        test_partial_state_remains_unpublishable() != 0 ||
        test_out_of_range_compare_enum_is_sticky_and_fail_closed() != 0 ||
        test_zcomp_flushes_before_raw_mutation() != 0) {
        return 1;
    }

    puts("pc GX raw Alpha/ZCompLoc shadow fixture: PASS");
    puts("proof boundary: setter-owned CPU Alpha provenance and flush ordering only; no packet version, renderer, Metal, device, or playability claim");
    return 0;
}
