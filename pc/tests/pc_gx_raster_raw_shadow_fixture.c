#include "pc_gx_internal.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void GXSetViewport(
    f32 left,
    f32 top,
    f32 wd,
    f32 ht,
    f32 nearz,
    f32 farz
);
extern void GXSetViewportJitter(
    f32 left,
    f32 top,
    f32 wd,
    f32 ht,
    f32 nearz,
    f32 farz,
    u32 field
);
extern void GXSetScissor(u32 left, u32 top, u32 wd, u32 ht);
extern void GXSetScissorBoxOffset(s32 x, s32 y);
extern void GXSetClipMode(u32 mode);
extern void GXSetCullMode(u32 mode);
extern void GXSetCoPlanar(GXBool enable);
extern void GXSetLineWidth(u8 width, u32 texOffsets);
extern void GXSetPointSize(u8 size, u32 texOffsets);
extern void GXEnableTexOffsets(u32 coord, GXBool line, GXBool point);
extern void GXSetDither(GXBool dither);
extern void GXSetDstAlpha(GXBool enable, u8 alpha);
extern void GXSetFieldMask(GXBool odd, GXBool even);
extern void GXSetFieldMode(GXBool field_mode, GXBool half_aspect);

/* The focused target links pc_gx.c without the full PC host executable. */
int g_pc_window_w = PC_SCREEN_WIDTH;
int g_pc_window_h = PC_SCREEN_HEIGHT;
int g_pc_widescreen_stretch = 0;
int g_pc_model_viewer_no_cull = 0;

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

static void fixture_gl_viewport(
    GLint x,
    GLint y,
    GLsizei width,
    GLsizei height
) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void fixture_gl_depth_range(GLdouble nearz, GLdouble farz) {
    (void)nearz;
    (void)farz;
}

static void fixture_gl_enable(GLenum capability) {
    (void)capability;
}

static void fixture_gl_scissor(
    GLint x,
    GLint y,
    GLsizei width,
    GLsizei height
) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void fixture_gl_line_width(GLfloat width) {
    (void)width;
}

static void fixture_gl_point_size(GLfloat size) {
    (void)size;
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static const PCGXRawRaster* raw_raster(void) {
    return pc_gx_raw_raster_shadow_fixture();
}

static uint32_t float_bits(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void reset_state(void) {
    pc_gx_clear_raster_flush_fixture_observer();
    memset(&g_gx, 0, sizeof(g_gx));
    pc_gx_viewport_state_invalidate();
    glad_glBindVertexArray = fixture_gl_bind_vertex_array;
    glad_glBindBuffer = fixture_gl_bind_buffer;
    glad_glViewport = fixture_gl_viewport;
    glad_glDepthRange = fixture_gl_depth_range;
    glad_glEnable = fixture_gl_enable;
    glad_glScissor = fixture_gl_scissor;
    glad_glLineWidth = fixture_gl_line_width;
    glad_glPointSize = fixture_gl_point_size;
    g_gx.cull_mode = GX_CULL_NONE;
    g_pc_model_viewer_no_cull = 0;
}

static void set_complete_raster_state(void) {
    uint32_t coord;

    GXSetViewport(-0.0f, 0.25f, 640.0f, 480.0f, 0.0f, 1.0f);
    GXSetScissor(0, 0, 640, 480);
    GXSetScissorBoxOffset(-342, 1705);
    GXSetClipMode(GX_CLIP_ENABLE);
    GXSetCullMode(GX_CULL_BACK);
    GXSetCoPlanar(GX_FALSE);
    GXSetLineWidth(255, GX_TO_SIXTEENTH);
    GXSetPointSize(0, GX_TO_ZERO);
    for (coord = 0; coord < 8; coord++) {
        GXEnableTexOffsets(
            coord,
            (GXBool)(coord & 1),
            (GXBool)(coord == 7)
        );
    }
    GXSetDither(GX_FALSE);
    GXSetDstAlpha(GX_TRUE, 255);
    GXSetFieldMask(GX_FALSE, GX_TRUE);
    GXSetFieldMode(GX_TRUE, GX_FALSE);
}

static int expect_complete_raster_state(void) {
    const PCGXRawRaster* shadow = raw_raster();

    CHECK(shadow->known_mask == PC_GX_RAW_RASTER_KNOWN_ALL);
    CHECK(shadow->line_texcoord_known_mask ==
          PC_GX_RAW_RASTER_TEXCOORD_KNOWN_ALL);
    CHECK(shadow->point_texcoord_known_mask ==
          PC_GX_RAW_RASTER_TEXCOORD_KNOWN_ALL);
    CHECK(shadow->invalid == 0);
    CHECK(shadow->value.viewport_bits[0] == float_bits(-0.0f));
    CHECK(shadow->value.viewport_bits[1] == float_bits(0.25f));
    CHECK(shadow->value.viewport_bits[2] == float_bits(640.0f));
    CHECK(shadow->value.viewport_bits[3] == float_bits(480.0f));
    CHECK(shadow->value.viewport_bits[4] == float_bits(0.0f));
    CHECK(shadow->value.viewport_bits[5] == float_bits(1.0f));
    CHECK(shadow->value.scissor[0] == 0);
    CHECK(shadow->value.scissor[1] == 0);
    CHECK(shadow->value.scissor[2] == 640);
    CHECK(shadow->value.scissor[3] == 480);
    CHECK(shadow->value.scissor_offset[0] == -342);
    CHECK(shadow->value.scissor_offset[1] == 1705);
    CHECK(shadow->value.clip_mode == GX_CLIP_ENABLE);
    CHECK(shadow->value.cull_mode == GX_CULL_BACK);
    CHECK(shadow->value.co_planar_enable == GX_FALSE);
    CHECK(shadow->value.line_width == 255);
    CHECK(shadow->value.line_tex_offsets == GX_TO_SIXTEENTH);
    CHECK(shadow->value.point_size == 0);
    CHECK(shadow->value.point_tex_offsets == GX_TO_ZERO);
    CHECK(shadow->value.line_texcoord_mask == UINT32_C(0xAA));
    CHECK(shadow->value.point_texcoord_mask == UINT32_C(0x80));
    CHECK(shadow->value.dither == GX_FALSE);
    CHECK(shadow->value.dst_alpha_enable == GX_TRUE);
    CHECK(shadow->value.dst_alpha == 255);
    CHECK(shadow->value.field_mode == GX_TRUE);
    CHECK(shadow->value.half_aspect_ratio == GX_FALSE);
    CHECK(shadow->value.field_odd_mask == GX_FALSE);
    CHECK(shadow->value.field_even_mask == GX_TRUE);
    return 0;
}

typedef struct {
    int calls;
    int in_begin;
    int current_vertex_idx;
    int pending_verts;
    PCGXRawRaster before;
} RasterFlushObservation;

static void observe_raster_flush(void* context) {
    RasterFlushObservation* observation =
        (RasterFlushObservation*)context;

    observation->calls++;
    observation->in_begin = g_gx.in_begin;
    observation->current_vertex_idx = g_gx.current_vertex_idx;
    observation->pending_verts = g_gx.pending_verts;
    observation->before = *raw_raster();
}

static void prepare_completed_batch(RasterFlushObservation* observation) {
    memset(observation, 0, sizeof(*observation));
    g_gx.in_begin = 1;
    g_gx.expected_vertex_count = 1;
    g_gx.current_vertex_idx = 1;
    g_gx.pending_verts = 0;
    g_gx.vertex_pending = 0;
    g_gx.current_primitive = GX_TRIANGLES;
    g_gx.pending_prim = GX_TRIANGLES;
    pc_gx_set_raster_flush_fixture_observer(
        observe_raster_flush,
        observation
    );
}

static int test_initial_unknownness_and_fail_closed(void) {
    AcgcGxCanonicalRasterState state;
    AcgcGxCanonicalRasterState sentinel;
    PCGXRawRaster zero;

    reset_state();
    memset(&zero, 0, sizeof(zero));
    CHECK(raw_raster() == &g_gx.raw_raster);
    CHECK(memcmp(raw_raster(), &zero, sizeof(zero)) == 0);

    memset(&sentinel, 0xA5, sizeof(sentinel));
    state = sentinel;
    CHECK(!pc_gx_raw_raster_build_canonical(&state));
    CHECK(memcmp(&state, &sentinel, sizeof(state)) == 0);
    CHECK(!pc_gx_raw_raster_build_canonical(NULL));
    return 0;
}

static int test_complete_state_builds_existing_canonical_raster(void) {
    AcgcGxCanonicalRasterState state;

    reset_state();
    set_complete_raster_state();
    CHECK(expect_complete_raster_state() == 0);
    CHECK(pc_gx_raw_raster_build_canonical(&state));
    CHECK(memcmp(&state, &raw_raster()->value, sizeof(state)) == 0);
    CHECK(acgc_gx_canonical_raster_state_validate(&state));
    CHECK(sizeof(state) == ACGC_GX_CANONICAL_RASTER_STATE_SIZE);
    return 0;
}

static int test_equal_legacy_values_establish_provenance(void) {
    AcgcGxCanonicalRasterState state;
    uint32_t dirty_before;

    reset_state();
    g_gx.viewport[0] = -0.0f;
    g_gx.viewport[1] = 0.25f;
    g_gx.viewport[2] = 640.0f;
    g_gx.viewport[3] = 480.0f;
    g_gx.viewport[4] = 0.0f;
    g_gx.viewport[5] = 1.0f;
    g_gx.scissor[0] = 0;
    g_gx.scissor[1] = 0;
    g_gx.scissor[2] = 640;
    g_gx.scissor[3] = 480;
    g_gx.cull_mode = GX_CULL_BACK;
    dirty_before = g_gx.dirty;
    set_complete_raster_state();
    CHECK(g_gx.dirty == dirty_before);
    CHECK(expect_complete_raster_state() == 0);
    CHECK(pc_gx_raw_raster_build_canonical(&state));
    return 0;
}

static int test_partial_state_remains_unpublishable(void) {
    AcgcGxCanonicalRasterState state;

    reset_state();
    GXSetViewport(0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
    GXSetScissor(0, 0, 640, 480);
    GXSetScissorBoxOffset(0, 0);
    GXSetClipMode(GX_CLIP_ENABLE);
    GXSetCullMode(GX_CULL_NONE);
    GXSetCoPlanar(GX_FALSE);
    GXSetLineWidth(6, GX_TO_ZERO);
    GXSetPointSize(6, GX_TO_ZERO);
    CHECK(raw_raster()->known_mask != PC_GX_RAW_RASTER_KNOWN_ALL);
    CHECK(!pc_gx_raw_raster_build_canonical(&state));
    return 0;
}

static int test_noop_setters_capture_logical_values(void) {
    uint32_t coord;

    reset_state();
    GXSetScissorBoxOffset(1, -1);
    GXSetClipMode(GX_CLIP_DISABLE);
    GXSetCoPlanar(GX_TRUE);
    GXSetDither(GX_TRUE);
    GXSetDstAlpha(GX_TRUE, 7);
    GXSetFieldMask(GX_TRUE, GX_FALSE);
    GXSetFieldMode(GX_FALSE, GX_TRUE);
    for (coord = 0; coord < 8; coord++) {
        GXEnableTexOffsets(coord, GX_TRUE, GX_FALSE);
    }

    CHECK(g_gx.dirty == 0);
    CHECK(raw_raster()->value.scissor_offset[0] == 1);
    CHECK(raw_raster()->value.scissor_offset[1] == -1);
    CHECK(raw_raster()->value.clip_mode == GX_CLIP_DISABLE);
    CHECK(raw_raster()->value.co_planar_enable == GX_TRUE);
    CHECK(raw_raster()->value.dither == GX_TRUE);
    CHECK(raw_raster()->value.dst_alpha_enable == GX_TRUE);
    CHECK(raw_raster()->value.dst_alpha == 7);
    CHECK(raw_raster()->value.field_odd_mask == GX_TRUE);
    CHECK(raw_raster()->value.field_even_mask == GX_FALSE);
    CHECK(raw_raster()->value.field_mode == GX_FALSE);
    CHECK(raw_raster()->value.half_aspect_ratio == GX_TRUE);
    CHECK(raw_raster()->value.line_texcoord_mask ==
          PC_GX_RAW_RASTER_TEXCOORD_KNOWN_ALL);
    CHECK(raw_raster()->value.point_texcoord_mask == 0);
    CHECK(raw_raster()->known_mask ==
          (PC_GX_RAW_RASTER_KNOWN_SCISSOR_OFFSET_X |
           PC_GX_RAW_RASTER_KNOWN_SCISSOR_OFFSET_Y |
           PC_GX_RAW_RASTER_KNOWN_CLIP_MODE |
           PC_GX_RAW_RASTER_KNOWN_CO_PLANAR |
           PC_GX_RAW_RASTER_KNOWN_LINE_TEXCOORD_MASK |
           PC_GX_RAW_RASTER_KNOWN_POINT_TEXCOORD_MASK |
           PC_GX_RAW_RASTER_KNOWN_DITHER |
           PC_GX_RAW_RASTER_KNOWN_DST_ALPHA_ENABLE |
           PC_GX_RAW_RASTER_KNOWN_DST_ALPHA |
           PC_GX_RAW_RASTER_KNOWN_FIELD_MODE |
           PC_GX_RAW_RASTER_KNOWN_HALF_ASPECT |
           PC_GX_RAW_RASTER_KNOWN_FIELD_ODD_MASK |
           PC_GX_RAW_RASTER_KNOWN_FIELD_EVEN_MASK));
    return 0;
}

static int test_invalid_domains_and_nonfinite_fail_closed(void) {
    AcgcGxCanonicalRasterState state;
    PCGXRawRaster before;
    PCGXRawRaster* writable;

    reset_state();
    GXSetClipMode(2);
    before = *raw_raster();
    CHECK(raw_raster()->invalid == 1);
    GXSetClipMode(GX_CLIP_ENABLE);
    CHECK(raw_raster()->invalid == 1);
    CHECK(memcmp(&raw_raster()->value, &before.value,
                 sizeof(before.value)) == 0);
    CHECK(!pc_gx_raw_raster_build_canonical(&state));

    reset_state();
    GXSetScissor(1706, 0, 0, 0);
    CHECK(raw_raster()->invalid == 1);

    reset_state();
    GXSetScissorBoxOffset(-343, 0);
    CHECK(raw_raster()->invalid == 1);

    reset_state();
    GXSetLineWidth(6, 6);
    CHECK(raw_raster()->invalid == 1);
    CHECK(raw_raster()->value.line_width == 0);
    CHECK(raw_raster()->value.line_tex_offsets == 0);

    reset_state();
    GXSetCullMode(4);
    CHECK(raw_raster()->invalid == 1);

    reset_state();
    set_complete_raster_state();
    writable = (PCGXRawRaster*)(void*)raw_raster();
    writable->value.viewport_bits[0] = UINT32_C(0x7FC00000);
    CHECK(!pc_gx_raw_raster_build_canonical(&state));
    return 0;
}

static int test_logical_values_precede_host_overrides(void) {
    reset_state();
    g_pc_model_viewer_no_cull = 1;
    GXSetCullMode(GX_CULL_BACK);
    g_pc_model_viewer_no_cull = 0;

    CHECK(raw_raster()->value.cull_mode == GX_CULL_BACK);
    CHECK(g_gx.cull_mode == GX_CULL_NONE);

    GXSetViewportJitter(
        13.0f, 17.0f, 320.0f, 240.0f, 0.125f, 0.875f, 0
    );
    GXSetScissor(7, 11, 300, 220);
    CHECK(raw_raster()->value.viewport_bits[0] == float_bits(13.0f));
    CHECK(raw_raster()->value.viewport_bits[1] == float_bits(17.0f));
    CHECK(raw_raster()->value.viewport_bits[2] == float_bits(320.0f));
    CHECK(raw_raster()->value.viewport_bits[3] == float_bits(240.0f));
    CHECK(raw_raster()->value.viewport_bits[4] == float_bits(0.125f));
    CHECK(raw_raster()->value.viewport_bits[5] == float_bits(0.875f));
    CHECK(raw_raster()->value.scissor[0] == 7);
    CHECK(raw_raster()->value.scissor[1] == 11);
    CHECK(raw_raster()->value.scissor[2] == 300);
    CHECK(raw_raster()->value.scissor[3] == 220);
    return 0;
}

static int test_flushes_before_raw_mutation(void) {
    RasterFlushObservation observation;

    reset_state();
    set_complete_raster_state();
    g_gx.dirty = 0;
    prepare_completed_batch(&observation);
    GXSetCullMode(GX_CULL_FRONT);
    pc_gx_clear_raster_flush_fixture_observer();

    CHECK(observation.calls == 1);
    CHECK(observation.in_begin == 0);
    CHECK(observation.current_vertex_idx == 1);
    CHECK(observation.pending_verts == 0);
    CHECK(observation.before.known_mask == PC_GX_RAW_RASTER_KNOWN_ALL);
    CHECK(observation.before.invalid == 0);
    CHECK(observation.before.value.cull_mode == GX_CULL_BACK);
    CHECK(g_gx.pending_verts == 1);
    CHECK(raw_raster()->value.cull_mode == GX_CULL_FRONT);
    CHECK(raw_raster()->known_mask == PC_GX_RAW_RASTER_KNOWN_ALL);
    CHECK(raw_raster()->invalid == 0);
    return 0;
}

int main(void) {
    if (test_initial_unknownness_and_fail_closed() != 0 ||
        test_complete_state_builds_existing_canonical_raster() != 0 ||
        test_equal_legacy_values_establish_provenance() != 0 ||
        test_partial_state_remains_unpublishable() != 0 ||
        test_noop_setters_capture_logical_values() != 0 ||
        test_invalid_domains_and_nonfinite_fail_closed() != 0 ||
        test_logical_values_precede_host_overrides() != 0 ||
        test_flushes_before_raw_mutation() != 0) {
        return 1;
    }

    puts("pc GX raw Raster shadow fixture: PASS");
    puts("proof boundary: setter-owned CPU Raster provenance and flush ordering only; no packet version, renderer, Metal, device, or playability claim");
    return 0;
}
