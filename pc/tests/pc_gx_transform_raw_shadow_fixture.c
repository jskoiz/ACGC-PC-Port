#include "pc_gx_internal.h"

#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXTransform.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The focused target links pc_gx.c without the full PC host executable. */
int g_pc_window_w = PC_SCREEN_WIDTH;
int g_pc_window_h = PC_SCREEN_HEIGHT;
int g_pc_widescreen_stretch = 0;

void pc_gx_tev_seq_reset(void) {
}

PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    return NULL;
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

static int exact_slot(uint32_t id) {
    int slot;

    for (slot = 0; slot < PC_GX_TRANSFORM_POSITION_COUNT; slot++) {
        if (id == (uint32_t)(slot * 3)) {
            return slot;
        }
    }
    return -1;
}

static int raw_transform_is_ready(void) {
    const PCGXRawTransform* shadow = &g_gx.raw_transform;
    int slot;
    int index;

    slot = exact_slot(shadow->current_position_id);
    if (shadow->invalid != 0 || shadow->projection.known == 0 ||
        shadow->current_position_known == 0 || slot < 0 ||
        shadow->position[slot].known == 0) {
        return 0;
    }
    for (index = 0; index < PC_GX_TRANSFORM_POSITION_COUNT; index++) {
        if (shadow->position_indexed_unresolved[index] != 0 ||
            shadow->normal_indexed_unresolved[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static void reset_state(void) {
    memset(&g_gx, 0, sizeof(g_gx));
    g_pc_window_w = PC_SCREEN_WIDTH;
    g_pc_window_h = PC_SCREEN_HEIGHT;
    g_pc_widescreen_stretch = 0;
    pc_gx_transform_fixture_set_aspect(0, 1.0f);
}

static int expect_projection(
    uint32_t type,
    const float coefficients[PC_GX_TRANSFORM_PROJECTION_WORDS]
) {
    int index;
    const PCGXRawProjection* actual = &g_gx.raw_transform.projection;

    if (actual->type != type || actual->known == 0) {
        return 0;
    }
    for (index = 0; index < PC_GX_TRANSFORM_PROJECTION_WORDS; index++) {
        if (actual->coefficients[index] != float_bits(coefficients[index])) {
            return 0;
        }
    }
    return 1;
}

static int expect_position(int slot, const float matrix[3][4]) {
    int word;
    const PCGXRawPositionMatrix* actual = &g_gx.raw_transform.position[slot];

    if (actual->known == 0) {
        return 0;
    }
    for (word = 0; word < PC_GX_TRANSFORM_POSITION_WORDS; word++) {
        if (actual->words[word] != float_bits(matrix[word / 4][word % 4])) {
            return 0;
        }
    }
    return 1;
}

static int expect_normal_3x4(int slot, const float matrix[3][4]) {
    static const int source_words[PC_GX_TRANSFORM_NORMAL_WORDS] = {
        0, 1, 2, 4, 5, 6, 8, 9, 10
    };
    int word;
    const PCGXRawNormalMatrix* actual = &g_gx.raw_transform.normal[slot];

    if (actual->known == 0) {
        return 0;
    }
    for (word = 0; word < PC_GX_TRANSFORM_NORMAL_WORDS; word++) {
        if (actual->words[word] != float_bits(
                ((const float*)matrix)[source_words[word]])) {
            return 0;
        }
    }
    return 1;
}

static int expect_normal_3x3(int slot, const float matrix[3][3]) {
    int word;
    const PCGXRawNormalMatrix* actual = &g_gx.raw_transform.normal[slot];

    if (actual->known == 0) {
        return 0;
    }
    for (word = 0; word < PC_GX_TRANSFORM_NORMAL_WORDS; word++) {
        if (actual->words[word] != float_bits(((const float*)matrix)[word])) {
            return 0;
        }
    }
    return 1;
}

static void make_position(float matrix[3][4], int slot) {
    int word;

    for (word = 0; word < PC_GX_TRANSFORM_POSITION_WORDS; word++) {
        ((float*)matrix)[word] = (float)(1000 + slot * 100 + word);
    }
}

static void make_normal(float matrix[3][4], int slot) {
    int word;

    for (word = 0; word < PC_GX_TRANSFORM_POSITION_WORDS; word++) {
        ((float*)matrix)[word] = (float)(2000 + slot * 100 + word);
    }
}

static int test_initial_unknownness(void) {
    int slot;

    reset_state();
    CHECK(g_gx.raw_transform.projection.known == 0);
    CHECK(g_gx.raw_transform.current_position_known == 0);
    CHECK(g_gx.raw_transform.invalid == 0);
    for (slot = 0; slot < PC_GX_TRANSFORM_POSITION_COUNT; slot++) {
        CHECK(g_gx.raw_transform.position[slot].known == 0);
        CHECK(g_gx.raw_transform.normal[slot].known == 0);
        CHECK(g_gx.raw_transform.position_indexed_unresolved[slot] == 0);
        CHECK(g_gx.raw_transform.normal_indexed_unresolved[slot] == 0);
    }
    CHECK(g_gx.projection_mtx[0][0] == 0.0f);
    CHECK(g_gx.pos_mtx[0][0][0] == 0.0f);
    CHECK(g_gx.nrm_mtx[0][0][0] == 0.0f);
    return 0;
}

static int test_projection_capture(void) {
    static const float perspective[4][4] = {
        {11.0f, 12.0f, 13.0f, 14.0f},
        {21.0f, 22.0f, 23.0f, 24.0f},
        {31.0f, 32.0f, 33.0f, 34.0f},
        {41.0f, 42.0f, 43.0f, 44.0f}
    };
    static const float perspective_coefficients[6] = {
        11.0f, 13.0f, 22.0f, 23.0f, 33.0f, 34.0f
    };
    static const float orthographic_vector[7] = {
        1.0f, 51.0f, 52.0f, 53.0f, 54.0f, 55.0f, 56.0f
    };
    static const float orthographic_coefficients[6] = {
        51.0f, 52.0f, 53.0f, 54.0f, 55.0f, 56.0f
    };
    static const float orthographic[4][4] = {
        {71.0f, 72.0f, 73.0f, 74.0f},
        {81.0f, 82.0f, 83.0f, 84.0f},
        {91.0f, 92.0f, 93.0f, 94.0f},
        {101.0f, 102.0f, 103.0f, 104.0f}
    };
    static const float orthographic_matrix_coefficients[6] = {
        71.0f, 74.0f, 82.0f, 84.0f, 93.0f, 94.0f
    };
    float host_before[4][4];

    reset_state();
    GXSetProjection(perspective, GX_PERSPECTIVE);
    CHECK(expect_projection(GX_PERSPECTIVE, perspective_coefficients));
    CHECK(g_gx.raw_transform.invalid == 0);
    CHECK(g_gx.projection_mtx[0][0] == 11.0f);
    CHECK(g_gx.projection_mtx[0][2] == 13.0f);
    CHECK(g_gx.projection_mtx[1][1] == 22.0f);
    CHECK(g_gx.projection_mtx[1][2] == 23.0f);
    CHECK(g_gx.projection_mtx[2][2] == 33.0f);
    CHECK(g_gx.projection_mtx[2][3] == 34.0f);
    CHECK(g_gx.projection_mtx[3][2] == -1.0f);
    CHECK(g_gx.projection_mtx[3][3] == 0.0f);

    /* The raw coefficients stay pre-widescreen while the existing host
     * projection follows the established aspect-adjustment path. */
    pc_gx_transform_fixture_set_aspect(1, 0.5f);
    GXSetProjection(perspective, GX_PERSPECTIVE);
    CHECK(expect_projection(GX_PERSPECTIVE, perspective_coefficients));
    CHECK(g_gx.projection_mtx[0][0] == 5.5f);
    CHECK(g_gx.raw_transform.projection.coefficients[0] ==
          float_bits(11.0f));

    pc_gx_transform_fixture_set_aspect(0, 1.0f);
    GXSetProjectionv(orthographic_vector);
    CHECK(expect_projection(GX_ORTHOGRAPHIC, orthographic_coefficients));
    CHECK(g_gx.raw_transform.invalid == 0);
    CHECK(g_gx.projection_mtx[0][0] == 51.0f);
    CHECK(g_gx.projection_mtx[0][3] == 52.0f);
    CHECK(g_gx.projection_mtx[1][1] == 53.0f);
    CHECK(g_gx.projection_mtx[1][3] == 54.0f);
    CHECK(g_gx.projection_mtx[2][2] == 55.0f);
    CHECK(g_gx.projection_mtx[2][3] == 56.0f);
    CHECK(g_gx.projection_mtx[3][2] == 0.0f);
    CHECK(g_gx.projection_mtx[3][3] == 1.0f);

    GXSetProjection(orthographic, GX_ORTHOGRAPHIC);
    CHECK(expect_projection(
        GX_ORTHOGRAPHIC,
        orthographic_matrix_coefficients
    ));
    CHECK(g_gx.raw_transform.invalid == 0);

    /* The raw sideband does not own texture matrices. */
    g_gx.tex_mtx[2][0][0] = 77.0f;
    memcpy(host_before, g_gx.projection_mtx, sizeof(host_before));
    GXSetCurrentMtx(0);
    CHECK(g_gx.tex_mtx[2][0][0] == 77.0f);
    CHECK(memcmp(host_before, g_gx.projection_mtx, sizeof(host_before)) == 0);
    return 0;
}

static int test_matrix_slots(void) {
    static const float projection[4][4] = {
        {61.0f, 62.0f, 63.0f, 64.0f},
        {65.0f, 66.0f, 67.0f, 68.0f},
        {69.0f, 70.0f, 71.0f, 72.0f},
        {73.0f, 74.0f, 75.0f, 76.0f}
    };
    float position[3][4];
    float normal[3][4];
    float normal_3x3[3][3] = {
        {301.0f, 302.0f, 303.0f},
        {304.0f, 305.0f, 306.0f},
        {307.0f, 308.0f, 309.0f}
    };
    float texture_before[3][4];
    int slot;

    reset_state();
    GXSetProjection(projection, GX_PERSPECTIVE);
    memcpy(texture_before, g_gx.tex_mtx[2], sizeof(texture_before));
    for (slot = 0; slot < PC_GX_TRANSFORM_POSITION_COUNT; slot++) {
        make_position(position, slot);
        make_normal(normal, slot);
        GXLoadPosMtxImm(position, (u32)(slot * 3));
        GXLoadNrmMtxImm(normal, (u32)(slot * 3));
        CHECK(expect_position(slot, position));
        CHECK(expect_normal_3x4(slot, normal));
    }
    GXLoadNrmMtxImm3x3(normal_3x3, GX_PNMTX9);
    CHECK(expect_normal_3x3(9, normal_3x3));
    for (slot = 0; slot < PC_GX_TRANSFORM_POSITION_COUNT; slot++) {
        GXSetCurrentMtx((u32)(slot * 3));
        CHECK(g_gx.raw_transform.current_position_known != 0);
        CHECK(g_gx.raw_transform.current_position_id == (u32)(slot * 3));
    }
    CHECK(g_gx.current_mtx == 9);
    CHECK(raw_transform_is_ready());
    CHECK(memcmp(texture_before, g_gx.tex_mtx[2], sizeof(texture_before)) == 0);
    return 0;
}

static int test_early_return_ordering(void) {
    float first[3][4] = {
        {401.0f, 0.0f, 403.0f, 404.0f},
        {405.0f, 406.0f, 407.0f, 408.0f},
        {409.0f, 410.0f, 411.0f, 412.0f}
    };
    float second[3][4];
    float host_before[3][3];
    uint32_t dirty_before;

    reset_state();
    memcpy(second, first, sizeof(second));
    second[0][1] = -0.0f;
    GXLoadNrmMtxImm(first, GX_PNMTX0);
    memcpy(host_before, g_gx.nrm_mtx[0], sizeof(host_before));
    dirty_before = g_gx.dirty;
    GXLoadNrmMtxImm(second, GX_PNMTX0);
    CHECK(g_gx.raw_transform.normal[0].known != 0);
    CHECK(g_gx.raw_transform.normal[0].words[1] == float_bits(-0.0f));
    CHECK(memcmp(host_before, g_gx.nrm_mtx[0], sizeof(host_before)) == 0);
    CHECK(g_gx.dirty == dirty_before);

    /* The host current slot starts at zero, but a real GXSetCurrentMtx(0)
     * still establishes a known logical ID in the raw state. */
    reset_state();
    dirty_before = g_gx.dirty;
    GXSetCurrentMtx(GX_PNMTX0);
    CHECK(g_gx.raw_transform.current_position_known != 0);
    CHECK(g_gx.raw_transform.current_position_id == GX_PNMTX0);
    CHECK(g_gx.current_mtx == 0);
    CHECK(g_gx.dirty == dirty_before);
    return 0;
}

static int test_invalid_and_nonfinite(void) {
    float position[3][4];
    float normal[3][4];
    float projection[4][4];
    float host_position[3][4];
    uint32_t dirty_before;

    reset_state();
    make_position(position, 0);
    GXLoadPosMtxImm(position, GX_PNMTX0);
    memcpy(host_position, g_gx.pos_mtx[0], sizeof(host_position));
    GXLoadPosMtxImm(position, 1); /* renderer keeps its historical /3 map */
    CHECK(g_gx.raw_transform.invalid != 0);
    CHECK(raw_transform_is_ready() == 0);
    CHECK(memcmp(host_position, g_gx.pos_mtx[0], sizeof(host_position)) == 0);

    reset_state();
    dirty_before = g_gx.dirty;
    GXSetCurrentMtx(1);
    CHECK(g_gx.raw_transform.current_position_known == 0);
    CHECK(g_gx.raw_transform.invalid != 0);
    CHECK(g_gx.current_mtx == 0);
    CHECK(g_gx.dirty == dirty_before);

    reset_state();
    make_position(position, 1);
    position[0][0] = NAN;
    GXLoadPosMtxImm(position, GX_PNMTX1);
    CHECK(g_gx.raw_transform.position[1].known == 0);
    CHECK(g_gx.raw_transform.position[1].words[0] == float_bits(NAN));
    CHECK(g_gx.raw_transform.invalid != 0);

    reset_state();
    make_normal(normal, 2);
    normal[1][1] = INFINITY;
    GXLoadNrmMtxImm(normal, GX_PNMTX2);
    CHECK(g_gx.raw_transform.normal[2].known == 0);
    CHECK(g_gx.raw_transform.normal[2].words[4] == float_bits(INFINITY));
    CHECK(g_gx.raw_transform.invalid != 0);

    reset_state();
    memset(projection, 0, sizeof(projection));
    projection[0][0] = 801.0f;
    projection[1][1] = 802.0f;
    projection[2][2] = 803.0f;
    projection[2][3] = 804.0f;
    GXSetProjection(projection, 99);
    CHECK(g_gx.raw_transform.projection.type == 99);
    CHECK(g_gx.raw_transform.projection.known == 0);
    CHECK(g_gx.raw_transform.invalid != 0);

    reset_state();
    memset(projection, 0, sizeof(projection));
    projection[0][0] = 901.0f;
    projection[1][1] = 902.0f;
    projection[2][2] = INFINITY;
    projection[2][3] = 904.0f;
    GXSetProjection(projection, GX_PERSPECTIVE);
    CHECK(g_gx.raw_transform.projection.known == 0);
    CHECK(g_gx.raw_transform.projection.coefficients[4] ==
          float_bits(INFINITY));
    CHECK(g_gx.raw_transform.invalid != 0);
    return 0;
}

static int test_indexed_unknownness(void) {
    static const float projection[4][4] = {
        {1001.0f, 1002.0f, 1003.0f, 1004.0f},
        {1005.0f, 1006.0f, 1007.0f, 1008.0f},
        {1009.0f, 1010.0f, 1011.0f, 1012.0f},
        {1013.0f, 1014.0f, 1015.0f, 1016.0f}
    };
    float position[3][4];
    float repaired_position[3][4];
    float normal[3][4];
    float repaired_normal[3][4];
    float position_before[3][4];
    float normal_before[3][3];
    int current_before;
    uint32_t dirty_before;

    reset_state();
    make_position(position, 0);
    make_normal(normal, 0);
    GXSetProjection(projection, GX_PERSPECTIVE);
    GXLoadPosMtxImm(position, GX_PNMTX0);
    GXLoadNrmMtxImm(normal, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    CHECK(raw_transform_is_ready());
    memcpy(position_before, g_gx.pos_mtx[0], sizeof(position_before));
    memcpy(normal_before, g_gx.nrm_mtx[0], sizeof(normal_before));
    current_before = g_gx.current_mtx;
    dirty_before = g_gx.dirty;

    GXLoadPosMtxIndx(UINT16_C(0x1234), GX_PNMTX0);
    GXLoadNrmMtxIndx3x3(UINT16_C(0x2345), GX_PNMTX0);
    CHECK(g_gx.raw_transform.position_indexed_unresolved[0] != 0);
    CHECK(g_gx.raw_transform.normal_indexed_unresolved[0] != 0);
    CHECK(g_gx.raw_transform.position[0].known == 0);
    CHECK(g_gx.raw_transform.normal[0].known == 0);
    CHECK(g_gx.raw_transform.position[0].words[0] == 0);
    CHECK(g_gx.raw_transform.normal[0].words[0] == 0);
    CHECK(raw_transform_is_ready() == 0);
    CHECK(memcmp(position_before, g_gx.pos_mtx[0], sizeof(position_before)) == 0);
    CHECK(memcmp(normal_before, g_gx.nrm_mtx[0], sizeof(normal_before)) == 0);
    CHECK(g_gx.current_mtx == current_before);
    CHECK(g_gx.dirty == dirty_before);

    /* A valid immediate overwrite repairs only its exact unresolved slot. */
    make_position(repaired_position, 7);
    make_normal(repaired_normal, 9);
    GXLoadPosMtxImm(repaired_position, GX_PNMTX0);
    CHECK(g_gx.raw_transform.position_indexed_unresolved[0] == 0);
    CHECK(g_gx.raw_transform.position[0].known != 0);
    CHECK(expect_position(0, repaired_position));
    CHECK(g_gx.raw_transform.normal_indexed_unresolved[0] != 0);
    CHECK(raw_transform_is_ready() == 0);

    /* An unresolved position in another slot survives repair of slot zero. */
    GXLoadPosMtxIndx(UINT16_C(0x3456), GX_PNMTX1);
    CHECK(g_gx.raw_transform.position_indexed_unresolved[1] != 0);
    GXLoadNrmMtxImm(repaired_normal, GX_PNMTX0);
    CHECK(g_gx.raw_transform.normal_indexed_unresolved[0] == 0);
    CHECK(g_gx.raw_transform.normal[0].known != 0);
    CHECK(expect_normal_3x4(0, repaired_normal));
    CHECK(g_gx.raw_transform.position_indexed_unresolved[1] != 0);
    CHECK(raw_transform_is_ready() == 0);

    make_position(repaired_position, 8);
    GXLoadPosMtxImm(repaired_position, GX_PNMTX1);
    CHECK(g_gx.raw_transform.position_indexed_unresolved[1] == 0);
    CHECK(raw_transform_is_ready() != 0);

    reset_state();
    GXLoadPosMtxIndx(UINT16_C(0x3456), 1);
    CHECK(g_gx.raw_transform.position_indexed_unresolved[0] == 0);
    CHECK(g_gx.raw_transform.invalid != 0);
    return 0;
}

int main(void) {
    CHECK(test_initial_unknownness() == 0);
    CHECK(test_projection_capture() == 0);
    CHECK(test_matrix_slots() == 0);
    CHECK(test_early_return_ordering() == 0);
    CHECK(test_invalid_and_nonfinite() == 0);
    CHECK(test_indexed_unknownness() == 0);
    puts("pc GX raw Transform shadow fixture: PASS");
    puts("proof boundary: setter-owned CPU raw Transform state and fail-closed indexed/invalid handling only; no canonical section, renderer, Metal, device, pixel, Windows runtime, or playability claim");
    return 0;
}
