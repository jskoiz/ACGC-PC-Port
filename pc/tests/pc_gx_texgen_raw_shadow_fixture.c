#include "pc_gx_internal.h"

#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXTransform.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* GXTexture.h in the pinned decomp does not publish this implementation's
 * declaration, but the GX source oracle defines the setter with this shape. */
extern void GXSetTexCoordCylWrap(u32 coord, u8 s_enable, u8 t_enable);
extern void GXSetNumTexGens(u8 n);
extern void GXSetTexCoordGen2(
    u32 dst,
    u32 func,
    u32 src,
    u32 mtx,
    GXBool normalize,
    u32 postmtx
);
extern void GXEnableTexOffsets(u32 coord, GXBool line, GXBool point);
extern void GXSetTexCoordScaleManually(u32 coord, GXBool enable, u16 ss, u16 ts);
extern void GXSetTexCoordBias(u32 coord, u8 s, u8 t);

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

static uint32_t float_bits(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void reset_state(void) {
    pc_gx_clear_texgen_flush_fixture_observer();
    memset(&g_gx, 0, sizeof(g_gx));
    glad_glBindVertexArray = fixture_gl_bind_vertex_array;
    glad_glBindBuffer = fixture_gl_bind_buffer;
    pc_gx_raw_texgen_shadow_reset_fixture();
}

typedef struct {
    int calls;
    int in_begin;
    int current_vertex_idx;
    uint32_t active_texgen_count;
    uint32_t active_texgen_count_known;
    uint32_t invalid;
    uint32_t texgen_function;
    uint32_t texgen_source;
    uint32_t texgen_ordinary_matrix_id;
    uint32_t texgen_normalize;
    uint32_t texgen_post_matrix_id;
    uint32_t ordinary_provenance;
    uint32_t ordinary_word0;
    uint32_t ordinary_known_word_mask;
    uint32_t su_manual_enable;
    uint32_t su_scale_s_raw_u16;
    uint32_t su_scale_t_raw_u16;
    uint32_t su_bias_s;
    uint32_t su_bias_t;
    uint32_t su_cylinder_s;
    uint32_t su_cylinder_t;
} FlushObservation;

static void observe_texgen_flush(void* context) {
    FlushObservation* observation = (FlushObservation*)context;
    const PCGXRawTexgen* shadow = &g_gx.raw_texgen;

    observation->calls++;
    observation->in_begin = g_gx.in_begin;
    observation->current_vertex_idx = g_gx.current_vertex_idx;
    observation->active_texgen_count = shadow->active_texgen_count;
    observation->active_texgen_count_known =
        shadow->active_texgen_count_known;
    observation->invalid = shadow->invalid;
    observation->texgen_function = shadow->texgen[0].function;
    observation->texgen_source = shadow->texgen[0].source;
    observation->texgen_ordinary_matrix_id =
        shadow->texgen[0].ordinary_matrix_id;
    observation->texgen_normalize = shadow->texgen[0].normalize;
    observation->texgen_post_matrix_id = shadow->texgen[0].post_matrix_id;
    observation->ordinary_provenance = shadow->ordinary[0].provenance;
    observation->ordinary_word0 = shadow->ordinary[0].words[0];
    observation->ordinary_known_word_mask =
        shadow->ordinary[0].known_word_mask;
    observation->su_manual_enable = shadow->su[0].manual_enable;
    observation->su_scale_s_raw_u16 = shadow->su[0].scale_s_raw_u16;
    observation->su_scale_t_raw_u16 = shadow->su[0].scale_t_raw_u16;
    observation->su_bias_s = shadow->su[0].bias_s;
    observation->su_bias_t = shadow->su[0].bias_t;
    observation->su_cylinder_s = shadow->su[0].cylinder_s;
    observation->su_cylinder_t = shadow->su[0].cylinder_t;

}

static void prepare_completed_batch(FlushObservation* observation) {
    memset(observation, 0, sizeof(*observation));
    g_gx.in_begin = 1;
    g_gx.expected_vertex_count = 1;
    g_gx.current_vertex_idx = 1;
    g_gx.pending_verts = 0;
    g_gx.vertex_pending = 0;
    g_gx.current_primitive = GX_TRIANGLES;
    g_gx.pending_prim = GX_TRIANGLES;
    pc_gx_set_texgen_flush_fixture_observer(
        observe_texgen_flush,
        observation
    );
}

static void finish_observed_batch(void) {
    pc_gx_clear_texgen_flush_fixture_observer();
}

static void make_matrix(float matrix[3][4], float base) {
    int word;

    for (word = 0; word < PC_GX_TEXGEN_MATRIX_WORD_COUNT; word++) {
        ((float*)matrix)[word] = base + (float)word;
    }
}

static int matrix_words_are_zero(const PCGXRawTexMatrix* record) {
    int word;

    for (word = 0; word < PC_GX_TEXGEN_MATRIX_WORD_COUNT; word++) {
        if (record->words[word] != 0) {
            return 0;
        }
    }
    return 1;
}

static int test_initial_unknownness_and_domains(void) {
    const PCGXRawTexgen* shadow;
    int index;

    reset_state();
    shadow = pc_gx_raw_texgen_shadow_fixture();
    CHECK(shadow == &g_gx.raw_texgen);
    CHECK(shadow->active_texgen_count_known == 0);
    CHECK(shadow->invalid == 0);
    for (index = 0; index < PC_GX_TEXGEN_COUNT; index++) {
        CHECK(shadow->texgen[index].component_known == 0);
        CHECK(shadow->su[index].component_known == 0);
    }
    for (index = 0; index < PC_GX_TEXGEN_ORDINARY_MATRIX_COUNT; index++) {
        uint32_t expected = index < 10 ?
            (uint32_t)(GX_TEXMTX0 + index * 3) : (uint32_t)GX_IDENTITY;
        const PCGXRawTexMatrix* record = &shadow->ordinary[index];

        CHECK(record->logical_id == expected);
        CHECK(record->slot_known == 0);
        CHECK(record->known_word_mask == 0);
        CHECK(record->last_written_word_count == 0);
        CHECK(matrix_words_are_zero(record));
    }
    for (index = 0; index < PC_GX_TEXGEN_POST_MATRIX_COUNT; index++) {
        uint32_t expected = index < 20 ?
            (uint32_t)(GX_PTTEXMTX0 + index * 3) : (uint32_t)GX_PTIDENTITY;
        const PCGXRawTexMatrix* record = &shadow->post[index];

        CHECK(record->logical_id == expected);
        CHECK(record->slot_known == 0);
        CHECK(record->known_word_mask == 0);
        CHECK(record->last_written_word_count == 0);
        CHECK(matrix_words_are_zero(record));
    }
    return 0;
}

static int test_matrix_ranges_and_identity_slots(void) {
    float first[3][4];
    float second[3][4];
    const PCGXRawTexMatrix* ordinary;
    const PCGXRawTexMatrix* post;
    int word;

    reset_state();
    make_matrix(first, 100.0f);
    make_matrix(second, 200.0f);
    GXLoadTexMtxImm(first, GX_TEXMTX0, GX_MTX3x4);
    GXLoadTexMtxImm(second, GX_TEXMTX0, GX_MTX2x4);
    ordinary = &g_gx.raw_texgen.ordinary[0];
    CHECK(ordinary->slot_known == 1);
    CHECK(ordinary->provenance == PC_GX_TEXGEN_MATRIX_PROVENANCE_IMMEDIATE);
    CHECK(ordinary->last_load_type == GX_MTX2x4);
    CHECK(ordinary->last_written_word_count == 8);
    CHECK(ordinary->known_word_mask == UINT32_C(0xFFF));
    for (word = 0; word < 8; word++) {
        CHECK(ordinary->words[word] == float_bits(((float*)second)[word]));
    }
    for (word = 8; word < 12; word++) {
        CHECK(ordinary->words[word] == float_bits(((float*)first)[word]));
    }

    GXLoadTexMtxImm(second, GX_IDENTITY, GX_MTX3x4);
    GXLoadTexMtxImm(first, GX_PTIDENTITY, GX_MTX3x4);
    ordinary = &g_gx.raw_texgen.ordinary[10];
    post = &g_gx.raw_texgen.post[20];
    CHECK(ordinary->logical_id == GX_IDENTITY);
    CHECK(ordinary->slot_known == 1);
    CHECK(ordinary->known_word_mask == UINT32_C(0xFFF));
    CHECK(ordinary->words[0] == float_bits(((float*)second)[0]));
    CHECK(post->logical_id == GX_PTIDENTITY);
    CHECK(post->slot_known == 1);
    CHECK(post->known_word_mask == UINT32_C(0xFFF));
    CHECK(post->words[11] == float_bits(((float*)first)[11]));
    CHECK(g_gx.raw_texgen.invalid == 0);
    return 0;
}

static int test_matrix_last_type_is_not_generator_type(void) {
    float full[3][4];
    float short_matrix[3][4];

    reset_state();
    make_matrix(full, 300.0f);
    make_matrix(short_matrix, 400.0f);
    GXLoadTexMtxImm(full, GX_TEXMTX1, GX_MTX3x4);
    GXLoadTexMtxImm(short_matrix, GX_TEXMTX1, GX_MTX2x4);
    GXLoadTexMtxImm(full, GX_PTIDENTITY, GX_MTX3x4);
    GXSetTexCoordGen2(
        GX_TEXCOORD0, GX_TG_MTX3x4, GX_TG_TEX0, GX_TEXMTX1,
        GX_FALSE, GX_PTIDENTITY
    );
    GXSetNumTexGens(1);
    CHECK(g_gx.raw_texgen.ordinary[1].last_load_type == GX_MTX2x4);
    CHECK(g_gx.raw_texgen.ordinary[1].known_word_mask == UINT32_C(0xFFF));
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 1);
    return 0;
}

static int test_temporal_state_ordering(void) {
    float old_matrix[3][4];
    float new_matrix[3][4];
    FlushObservation observation;

    reset_state();
    make_matrix(old_matrix, 1100.0f);
    make_matrix(new_matrix, 1200.0f);
    GXLoadTexMtxImm(old_matrix, GX_TEXMTX0, GX_MTX3x4);
    prepare_completed_batch(&observation);
    GXLoadTexMtxImm(new_matrix, GX_TEXMTX0, GX_MTX3x4);
    finish_observed_batch();
    CHECK(observation.calls == 1);
    CHECK(g_gx.pending_verts == 1);
    CHECK(observation.in_begin == 0);
    CHECK(observation.current_vertex_idx == 1);
    CHECK(observation.ordinary_provenance ==
          PC_GX_TEXGEN_MATRIX_PROVENANCE_IMMEDIATE);
    CHECK(observation.ordinary_word0 == float_bits(old_matrix[0][0]));
    CHECK(observation.ordinary_known_word_mask == UINT32_C(0xFFF));
    CHECK(g_gx.raw_texgen.ordinary[0].words[0] ==
          float_bits(new_matrix[0][0]));

    reset_state();
    GXLoadTexMtxImm(old_matrix, GX_TEXMTX0, GX_MTX3x4);
    prepare_completed_batch(&observation);
    GXLoadTexMtxIndx(17, GX_TEXMTX0, GX_MTX2x4);
    finish_observed_batch();
    CHECK(observation.calls == 1);
    CHECK(g_gx.pending_verts == 1);
    CHECK(observation.ordinary_provenance ==
          PC_GX_TEXGEN_MATRIX_PROVENANCE_IMMEDIATE);
    CHECK(observation.ordinary_word0 == float_bits(old_matrix[0][0]));
    CHECK(observation.ordinary_known_word_mask == UINT32_C(0xFFF));
    CHECK(g_gx.raw_texgen.ordinary[0].provenance ==
          PC_GX_TEXGEN_MATRIX_PROVENANCE_INDEXED_UNRESOLVED);
    CHECK(g_gx.raw_texgen.ordinary[0].known_word_mask ==
          UINT32_C(0xF00));

    reset_state();
    GXSetNumTexGens(1);
    prepare_completed_batch(&observation);
    GXSetNumTexGens(2);
    finish_observed_batch();
    CHECK(observation.calls == 1);
    CHECK(g_gx.pending_verts == 1);
    CHECK(observation.active_texgen_count == 1);
    CHECK(observation.active_texgen_count_known == 1);
    CHECK(g_gx.raw_texgen.active_texgen_count == 2);

    reset_state();
    GXSetTexCoordGen2(
        0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY
    );
    prepare_completed_batch(&observation);
    GXSetTexCoordGen2(
        0, GX_TG_MTX3x4, GX_TG_NRM, GX_TEXMTX0, GX_TRUE, GX_PTTEXMTX0
    );
    finish_observed_batch();
    CHECK(observation.calls == 1);
    CHECK(g_gx.pending_verts == 1);
    CHECK(observation.texgen_function == GX_TG_MTX2x4);
    CHECK(observation.texgen_source == GX_TG_TEX0);
    CHECK(observation.texgen_ordinary_matrix_id == GX_IDENTITY);
    CHECK(observation.texgen_normalize == GX_FALSE);
    CHECK(observation.texgen_post_matrix_id == GX_PTIDENTITY);
    CHECK(g_gx.raw_texgen.texgen[0].function == GX_TG_MTX3x4);
    CHECK(g_gx.raw_texgen.texgen[0].source == GX_TG_NRM);
    CHECK(g_gx.raw_texgen.texgen[0].ordinary_matrix_id == GX_TEXMTX0);
    CHECK(g_gx.raw_texgen.texgen[0].normalize == GX_TRUE);
    CHECK(g_gx.raw_texgen.texgen[0].post_matrix_id == GX_PTTEXMTX0);

    reset_state();
    GXSetTexCoordScaleManually(0, GX_TRUE, 2, 3);
    prepare_completed_batch(&observation);
    GXSetTexCoordScaleManually(0, GX_TRUE, 4, 5);
    finish_observed_batch();
    CHECK(observation.calls == 1);
    CHECK(g_gx.pending_verts == 1);
    CHECK(observation.su_manual_enable == 1);
    CHECK(observation.su_scale_s_raw_u16 == 1);
    CHECK(observation.su_scale_t_raw_u16 == 2);
    CHECK(g_gx.raw_texgen.su[0].scale_s_raw_u16 == 3);
    CHECK(g_gx.raw_texgen.su[0].scale_t_raw_u16 == 4);

    reset_state();
    GXSetTexCoordCylWrap(0, 1, 0);
    prepare_completed_batch(&observation);
    GXSetTexCoordCylWrap(0, 0, 1);
    finish_observed_batch();
    CHECK(observation.calls == 1);
    CHECK(g_gx.pending_verts == 1);
    CHECK(observation.su_cylinder_s == 1);
    CHECK(observation.su_cylinder_t == 0);
    CHECK(g_gx.raw_texgen.su[0].cylinder_s == 0);
    CHECK(g_gx.raw_texgen.su[0].cylinder_t == 1);

    reset_state();
    GXSetTexCoordBias(0, 1, 0);
    prepare_completed_batch(&observation);
    GXSetTexCoordBias(0, 0, 1);
    finish_observed_batch();
    CHECK(observation.calls == 1);
    CHECK(g_gx.pending_verts == 1);
    CHECK(observation.su_bias_s == 1);
    CHECK(observation.su_bias_t == 0);
    CHECK(g_gx.raw_texgen.su[0].bias_s == 0);
    CHECK(g_gx.raw_texgen.su[0].bias_t == 1);
    return 0;
}

static int test_indexed_unknownness_is_targeted(void) {
    float full[3][4];
    float repair[3][4];
    const PCGXRawTexMatrix* ordinary;
    const PCGXRawTexMatrix* post;
    int word;

    reset_state();
    make_matrix(full, 500.0f);
    make_matrix(repair, 600.0f);
    GXLoadTexMtxImm(full, GX_TEXMTX2, GX_MTX3x4);
    GXLoadTexMtxIndx(17, GX_TEXMTX2, GX_MTX2x4);
    ordinary = &g_gx.raw_texgen.ordinary[2];
    CHECK(ordinary->slot_known == 1);
    CHECK(ordinary->provenance ==
          PC_GX_TEXGEN_MATRIX_PROVENANCE_INDEXED_UNRESOLVED);
    CHECK(ordinary->last_written_word_count == 8);
    CHECK(ordinary->known_word_mask == UINT32_C(0xF00));
    for (word = 0; word < 8; word++) {
        CHECK(ordinary->words[word] == 0);
    }
    for (word = 8; word < 12; word++) {
        CHECK(ordinary->words[word] == float_bits(((float*)full)[word]));
    }
    CHECK(g_gx.raw_texgen.invalid == 0);

    GXLoadTexMtxImm(repair, GX_TEXMTX2, GX_MTX2x4);
    CHECK(ordinary->provenance == PC_GX_TEXGEN_MATRIX_PROVENANCE_IMMEDIATE);
    CHECK(ordinary->known_word_mask == UINT32_C(0xFFF));
    CHECK(ordinary->words[0] == float_bits(repair[0][0]));

    GXLoadTexMtxImm(full, GX_PTIDENTITY, GX_MTX3x4);
    GXLoadTexMtxIndx(18, GX_PTIDENTITY, GX_MTX3x4);
    post = &g_gx.raw_texgen.post[20];
    CHECK(post->slot_known == 1);
    CHECK(post->provenance ==
          PC_GX_TEXGEN_MATRIX_PROVENANCE_INDEXED_UNRESOLVED);
    CHECK(post->last_written_word_count == 12);
    CHECK(post->known_word_mask == 0);
    CHECK(matrix_words_are_zero(post));
    CHECK(g_gx.raw_texgen.invalid == 0);

    GXLoadTexMtxIndx(19, GX_PTIDENTITY, GX_MTX2x4);
    CHECK(g_gx.raw_texgen.invalid == 1);
    CHECK(post->provenance == PC_GX_TEXGEN_MATRIX_PROVENANCE_INVALID);
    CHECK(post->last_written_word_count == 0);
    CHECK(post->known_word_mask == 0);
    CHECK(matrix_words_are_zero(post));
    return 0;
}

static int test_nonfinite_and_post_type_fail_closed(void) {
    float good[3][4];
    float bad[3][4];
    const PCGXRawTexMatrix* ordinary;

    reset_state();
    make_matrix(good, 700.0f);
    make_matrix(bad, 800.0f);
    bad[0][0] = NAN;
    GXLoadTexMtxImm(good, GX_TEXMTX3, GX_MTX3x4);
    GXLoadTexMtxImm(bad, GX_TEXMTX3, GX_MTX2x4);
    ordinary = &g_gx.raw_texgen.ordinary[3];
    CHECK(g_gx.raw_texgen.invalid == 1);
    CHECK(ordinary->provenance == PC_GX_TEXGEN_MATRIX_PROVENANCE_INVALID);
    CHECK(ordinary->last_written_word_count == 0);
    CHECK(ordinary->known_word_mask == 0);
    CHECK(matrix_words_are_zero(ordinary));

    reset_state();
    GXLoadTexMtxImm(good, GX_PTIDENTITY, GX_MTX2x4);
    CHECK(g_gx.raw_texgen.invalid == 1);
    CHECK(g_gx.raw_texgen.post[20].last_written_word_count == 0);
    CHECK(g_gx.raw_texgen.post[20].known_word_mask == 0);
    CHECK(matrix_words_are_zero(&g_gx.raw_texgen.post[20]));
    return 0;
}

static void load_active_matrix_domains(void) {
    float matrix[3][4];

    make_matrix(matrix, 900.0f);
    GXLoadTexMtxImm(matrix, GX_TEXMTX0, GX_MTX3x4);
    GXLoadTexMtxImm(matrix, GX_IDENTITY, GX_MTX3x4);
    GXLoadTexMtxImm(matrix, GX_PTIDENTITY, GX_MTX3x4);
}

static int test_active_prefix_order_and_counts(void) {
    uint32_t dirty_before;

    reset_state();
    load_active_matrix_domains();
    GXSetTexCoordGen2(
        0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY
    );
    CHECK(g_gx.raw_texgen.active_texgen_count_known == 0);
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 0);
    GXSetNumTexGens(1);
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 1);
    dirty_before = g_gx.dirty;
    GXSetTexCoordGen2(
        0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY
    );
    CHECK(g_gx.raw_texgen.texgen[0].component_known ==
          PC_GX_TEXGEN_KNOWN_ALL);
    CHECK(g_gx.dirty == dirty_before);

    /* A known inactive record is retained and does not become active. */
    GXSetTexCoordGen2(
        7, GX_TG_SRTG, GX_TG_COLOR1, GX_IDENTITY, GX_TRUE, GX_PTIDENTITY
    );
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 1);

    reset_state();
    load_active_matrix_domains();
    GXSetTexCoordGen2(0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(1, GX_TG_BUMP0, GX_TG_TEXCOORD0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(2, GX_TG_SRTG, GX_TG_COLOR0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetNumTexGens(3);
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 1);

    GXSetTexCoordGen2(3, GX_TG_BUMP1, GX_TG_TEXCOORD1, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetNumTexGens(4);
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 0);

    reset_state();
    load_active_matrix_domains();
    GXSetTexCoordGen2(0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(1, GX_TG_BUMP0, GX_TG_TEXCOORD0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(2, GX_TG_BUMP1, GX_TG_TEXCOORD0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(3, GX_TG_BUMP2, GX_TG_TEXCOORD0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetNumTexGens(4);
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 1);
    GXSetTexCoordGen2(4, GX_TG_BUMP3, GX_TG_TEXCOORD3, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetNumTexGens(5);
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 0);
    return 0;
}

static int test_bump_source_resolution(void) {
    reset_state();
    load_active_matrix_domains();
    GXSetTexCoordGen2(0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(1, GX_TG_BUMP0, GX_TG_TEXCOORD0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetNumTexGens(2);
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 1);
    CHECK(g_gx.raw_texgen.invalid == 0);

    /* A BUMP generator cannot resolve itself or a forward active source. */
    reset_state();
    load_active_matrix_domains();
    GXSetTexCoordGen2(0, GX_TG_BUMP0, GX_TG_TEXCOORD0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetNumTexGens(1);
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 0);
    CHECK(g_gx.raw_texgen.invalid == 0);

    /* A source record with no known components is not resolvable. */
    reset_state();
    load_active_matrix_domains();
    GXSetTexCoordGen2(0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(1, GX_TG_BUMP0, GX_TG_TEXCOORD0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    memset(&g_gx.raw_texgen.texgen[0], 0,
           sizeof(g_gx.raw_texgen.texgen[0]));
    GXSetNumTexGens(2);
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 0);
    CHECK(g_gx.raw_texgen.invalid == 0);

    /* A prior BUMP record is not a regular source generator. */
    reset_state();
    load_active_matrix_domains();
    GXSetTexCoordGen2(0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(1, GX_TG_BUMP0, GX_TG_TEXCOORD0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(2, GX_TG_BUMP1, GX_TG_TEXCOORD1, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetNumTexGens(3);
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 0);
    CHECK(g_gx.raw_texgen.invalid == 0);
    return 0;
}

static int test_color_and_function_validation(void) {
    reset_state();
    load_active_matrix_domains();
    GXSetTexCoordGen2(0, GX_TG_SRTG, GX_TG_COLOR1, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetNumTexGens(1);
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 0);

    reset_state();
    load_active_matrix_domains();
    GXSetTexCoordGen2(0, GX_TG_SRTG, GX_TG_COLOR0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordGen2(1, GX_TG_SRTG, GX_TG_COLOR1, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetNumTexGens(2);
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 1);
    GXSetTexCoordGen2(2, GX_TG_SRTG, GX_TG_COLOR0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetNumTexGens(3);
    CHECK(pc_gx_raw_texgen_shadow_valid_fixture() == 0);

    reset_state();
    load_active_matrix_domains();
    GXSetTexCoordGen2(0, GX_TG_BUMP0, GX_TG_TEX0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    CHECK(g_gx.raw_texgen.invalid == 1);
    CHECK(g_gx.raw_texgen.texgen[0].component_known == 0);

    reset_state();
    GXSetNumTexGens(9);
    CHECK(g_gx.raw_texgen.active_texgen_count_known == 0);
    CHECK(g_gx.raw_texgen.invalid == 1);
    CHECK(g_gx.num_tex_gens == 9);

    /* Raw fail-closed clearing must not erase the compatibility mirrors.
     * The second identical malformed call must therefore take the existing
     * equality return path instead of manufacturing another dirty transition. */
    reset_state();
    GXSetTexCoordGen2(0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    g_gx.dirty = 0;
    GXSetTexCoordGen2(0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_FALSE, 124);
    CHECK(g_gx.raw_texgen.invalid == 1);
    CHECK(g_gx.raw_texgen.texgen[0].component_known == 0);
    CHECK(g_gx.tex_gen_type[0] == GX_TG_MTX2x4);
    CHECK(g_gx.tex_gen_src[0] == GX_TG_TEX0);
    CHECK(g_gx.tex_gen_mtx[0] == GX_IDENTITY);
    g_gx.dirty = 0;
    GXSetTexCoordGen2(0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_FALSE, 124);
    CHECK(g_gx.dirty == 0);
    CHECK(g_gx.raw_texgen.texgen[0].component_known == 0);
    return 0;
}

static int test_su_provenance_and_raster_separation(void) {
    const PCGXRawTexcoordSU* su;
    PCGXRawTexcoordSU before;

    reset_state();
    GXSetTexCoordScaleManually(0, GX_TRUE, 0, 1);
    su = &g_gx.raw_texgen.su[0];
    CHECK((su->component_known & PC_GX_TEXGEN_SU_KNOWN_MANUAL) != 0);
    CHECK((su->component_known & PC_GX_TEXGEN_SU_KNOWN_SCALE_S) != 0);
    CHECK((su->component_known & PC_GX_TEXGEN_SU_KNOWN_SCALE_T) != 0);
    CHECK(su->manual_enable == 1);
    CHECK(su->scale_s_raw_u16 == UINT32_C(0xFFFF));
    CHECK(su->scale_t_raw_u16 == 0);

    GXSetTexCoordBias(0, 1, 0);
    GXSetTexCoordCylWrap(0, 0, 1);
    CHECK(su->bias_s == 1 && su->bias_t == 0);
    CHECK(su->cylinder_s == 0 && su->cylinder_t == 1);
    CHECK((su->component_known & (PC_GX_TEXGEN_SU_KNOWN_BIAS_S |
          PC_GX_TEXGEN_SU_KNOWN_BIAS_T |
          PC_GX_TEXGEN_SU_KNOWN_CYLINDER_S |
          PC_GX_TEXGEN_SU_KNOWN_CYLINDER_T)) ==
          (PC_GX_TEXGEN_SU_KNOWN_BIAS_S |
           PC_GX_TEXGEN_SU_KNOWN_BIAS_T |
           PC_GX_TEXGEN_SU_KNOWN_CYLINDER_S |
           PC_GX_TEXGEN_SU_KNOWN_CYLINDER_T));

    GXSetTexCoordScaleManually(0, GX_FALSE, 0, 0);
    CHECK(su->manual_enable == 0);
    CHECK(su->scale_s_raw_u16 == UINT32_C(0xFFFF));
    CHECK(su->scale_t_raw_u16 == 0);
    CHECK(su->bias_s == 1 && su->cylinder_t == 1);

    before = *su;
    GXEnableTexOffsets(0, GX_TRUE, GX_TRUE);
    CHECK(memcmp(&before, su, sizeof(before)) == 0);

    /* GXBool is bool on TARGET_PC, so malformed values cannot cross this
     * typed public boundary without changing its signature.  The u8 bias
     * fields still exercise targeted malformed-input clearing. */
    GXSetTexCoordBias(0, 2, 0);
    CHECK(g_gx.raw_texgen.invalid == 1);
    CHECK((su->component_known & PC_GX_TEXGEN_SU_KNOWN_MANUAL) != 0);
    CHECK((su->component_known & (PC_GX_TEXGEN_SU_KNOWN_BIAS_S |
          PC_GX_TEXGEN_SU_KNOWN_BIAS_T)) == 0);
    CHECK(su->bias_s == 0 && su->cylinder_t == 1);

    reset_state();
    GXSetTexCoordBias(8, 0, 0);
    CHECK(g_gx.raw_texgen.invalid == 1);
    CHECK(g_gx.raw_texgen.su[0].component_known == 0);
    return 0;
}

static int test_other_raw_shadows_untouched(void) {
    PCGXRawTransform transform_before;
    PCGXTevRawColor tev_before[4];
    PCGXTevRawColor kcolor_before[4];
    float matrix[3][4];

    reset_state();
    memset(&g_gx.raw_transform, 0xA5, sizeof(g_gx.raw_transform));
    memset(g_gx.tev_raw_colors, 0x5A, sizeof(g_gx.tev_raw_colors));
    memset(g_gx.tev_raw_k_colors, 0x3C, sizeof(g_gx.tev_raw_k_colors));
    memcpy(&transform_before, &g_gx.raw_transform, sizeof(transform_before));
    memcpy(tev_before, g_gx.tev_raw_colors, sizeof(tev_before));
    memcpy(kcolor_before, g_gx.tev_raw_k_colors, sizeof(kcolor_before));

    make_matrix(matrix, 1000.0f);
    GXLoadTexMtxImm(matrix, GX_IDENTITY, GX_MTX3x4);
    GXSetTexCoordGen2(0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_FALSE, GX_PTIDENTITY);
    GXSetTexCoordScaleManually(0, GX_TRUE, 2, 3);
    GXSetTexCoordBias(0, 0, 1);
    GXSetTexCoordCylWrap(0, 1, 0);
    CHECK(memcmp(&transform_before, &g_gx.raw_transform,
                 sizeof(transform_before)) == 0);
    CHECK(memcmp(tev_before, g_gx.tev_raw_colors, sizeof(tev_before)) == 0);
    CHECK(memcmp(kcolor_before, g_gx.tev_raw_k_colors,
                 sizeof(kcolor_before)) == 0);
    return 0;
}

int main(void) {
    if (test_initial_unknownness_and_domains() != 0 ||
        test_matrix_ranges_and_identity_slots() != 0 ||
        test_matrix_last_type_is_not_generator_type() != 0 ||
        test_temporal_state_ordering() != 0 ||
        test_indexed_unknownness_is_targeted() != 0 ||
        test_nonfinite_and_post_type_fail_closed() != 0 ||
        test_active_prefix_order_and_counts() != 0 ||
        test_bump_source_resolution() != 0 ||
        test_color_and_function_validation() != 0 ||
        test_su_provenance_and_raster_separation() != 0 ||
        test_other_raw_shadows_untouched() != 0) {
        return 1;
    }

    puts("pc GX raw Texgen/SU shadow fixture: PASS");
    puts("proof boundary: setter-owned CPU Texgen, matrix, and SU provenance with targeted fail-closed state only; no neutral ABI, producer, renderer, Metal, device, pixel, Windows runtime, or playability claim");
    return 0;
}
