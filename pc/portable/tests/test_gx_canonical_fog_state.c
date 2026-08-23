#include "acgc/gx_canonical_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static void fill_active_state(AcgcGxCanonicalFogState* state) {
    uint32_t index;

    memset(state, 0, sizeof(*state));
    state->fog_type = ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN;
    state->start_bits = UINT32_C(0x3F800000); /* 1.0f */
    state->end_bits = UINT32_C(0x40000000);   /* 2.0f */
    state->near_bits = UINT32_C(0x3F000000);  /* 0.5f */
    state->far_bits = UINT32_C(0x40A00000);   /* 5.0f */
    state->color_rgba8 = UINT32_C(0x44332211);
    state->range_adjust_enable = 1;
    state->range_center = 320;
    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        state->range_adjust[index] = UINT32_C(0x100) + index;
    }
}

static uint32_t read_le32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static int accepts_canonical_fog_types(void) {
    static const uint32_t fog_types[] = {
        ACGC_GX_CANONICAL_FOG_TYPE_NONE,
        ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN,
        ACGC_GX_CANONICAL_FOG_TYPE_PERSP_EXP,
        ACGC_GX_CANONICAL_FOG_TYPE_PERSP_EXP2,
        ACGC_GX_CANONICAL_FOG_TYPE_PERSP_REVEXP,
        ACGC_GX_CANONICAL_FOG_TYPE_PERSP_REVEXP2,
        ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_LIN,
        ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_EXP,
        ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_EXP2,
        ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_REVEXP,
        ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_REVEXP2,
    };
    AcgcGxCanonicalFogState state;
    size_t index;

    fill_active_state(&state);
    for (index = 0; index < sizeof(fog_types) / sizeof(fog_types[0]); index++) {
        state.fog_type = fog_types[index];
        CHECK(acgc_gx_canonical_fog_state_validate(&state));
    }

    state.fog_type = 1;
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));
    state.fog_type = 3;
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));
    state.fog_type = 16;
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));
    return 1;
}

static int accepts_denominator_degenerate_active_fog(void) {
    AcgcGxCanonicalFogState state;
    AcgcGxCanonicalFogState before;

    fill_active_state(&state);
    state.far_bits = state.near_bits;
    before = state;
    CHECK(acgc_gx_canonical_fog_state_validate(&state));
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);

    fill_active_state(&state);
    state.end_bits = state.start_bits;
    CHECK(acgc_gx_canonical_fog_state_validate(&state));
    return 1;
}

static int rejects_invalid_active_parameters(void) {
    AcgcGxCanonicalFogState state;

    fill_active_state(&state);
    state.start_bits = UINT32_C(0x7F800000); /* +infinity */
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));

    fill_active_state(&state);
    state.end_bits = UINT32_C(0x7FC00001); /* NaN */
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));

    fill_active_state(&state);
    state.near_bits = UINT32_C(0xFF800000); /* -infinity */
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));

    fill_active_state(&state);
    state.far_bits = UINT32_C(0x7FC00001); /* NaN */
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));

    fill_active_state(&state);
    state.far_bits = UINT32_C(0xBF800000); /* -1.0f */
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));

    fill_active_state(&state);
    state.near_bits = UINT32_C(0x40C00000); /* 6.0f */
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));
    return 1;
}

static int checks_range_and_reserved_contract(void) {
    AcgcGxCanonicalFogState state;

    CHECK(!acgc_gx_canonical_fog_state_validate(NULL));

    fill_active_state(&state);
    state.range_adjust_enable = 2;
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));

    fill_active_state(&state);
    state.range_center = ACGC_GX_CANONICAL_FOG_CENTER_MAX;
    CHECK(acgc_gx_canonical_fog_state_validate(&state));
    state.range_center++;
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));

    fill_active_state(&state);
    state.range_center = ACGC_GX_CANONICAL_FOG_CENTER_SOURCE_MAX + 1;
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));

    fill_active_state(&state);
    state.range_adjust[0] = ACGC_GX_CANONICAL_FOG_RANGE_VALUE_MASK;
    CHECK(acgc_gx_canonical_fog_state_validate(&state));
    state.range_adjust[0]++;
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));

    fill_active_state(&state);
    state.reserved[0] = 1;
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));
    state.reserved[0] = 0;
    state.reserved[1] = UINT32_C(0xFFFFFFFF);
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));

    fill_active_state(&state);
    state.range_adjust_enable = 0;
    state.range_center = ACGC_GX_CANONICAL_FOG_CENTER_MAX + 1;
    CHECK(acgc_gx_canonical_fog_state_validate(&state));
    return 1;
}

static int preserves_inactive_disabled_parameters(void) {
    AcgcGxCanonicalFogState state;
    AcgcGxCanonicalFogState before;

    fill_active_state(&state);
    state.fog_type = ACGC_GX_CANONICAL_FOG_TYPE_NONE;
    state.start_bits = UINT32_C(0x7FC00001);
    state.end_bits = UINT32_C(0xFF800000);
    state.near_bits = UINT32_C(0x7F800000);
    state.far_bits = UINT32_C(0xFFC00000);
    state.range_adjust_enable = 0;
    state.range_center = ACGC_GX_CANONICAL_FOG_CENTER_SOURCE_MAX;
    before = state;
    CHECK(acgc_gx_canonical_fog_state_validate(&state));
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);

    state.far_bits = UINT32_C(0xBF800000); /* finite but rejected by GX */
    CHECK(!acgc_gx_canonical_fog_state_validate(&state));
    return 1;
}

static int encodes_all_fog_words_and_preserves_output(void) {
    AcgcGxCanonicalFogState state;
    uint8_t bytes[ACGC_GX_CANONICAL_FOG_STATE_SIZE];
    uint8_t before[sizeof(bytes)];
    uint8_t short_bytes[ACGC_GX_CANONICAL_FOG_STATE_SIZE - 1];
    uint8_t short_before[sizeof(short_bytes)];
    uint32_t index;

    CHECK(sizeof(bytes) == ACGC_GX_CANONICAL_FOG_STATE_SIZE);
    fill_active_state(&state);
    CHECK(acgc_gx_canonical_fog_state_encode(
        &state, bytes, sizeof(bytes)));
    CHECK(read_le32(bytes + 0) == state.fog_type);
    CHECK(read_le32(bytes + 4) == state.start_bits);
    CHECK(read_le32(bytes + 8) == state.end_bits);
    CHECK(read_le32(bytes + 12) == state.near_bits);
    CHECK(read_le32(bytes + 16) == state.far_bits);
    CHECK(read_le32(bytes + 20) == state.color_rgba8);
    CHECK(read_le32(bytes + 24) == state.range_adjust_enable);
    CHECK(read_le32(bytes + 28) == state.range_center);
    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        CHECK(read_le32(bytes + 32 + index * sizeof(uint32_t)) ==
              state.range_adjust[index]);
    }
    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RESERVED_WORD_COUNT; index++) {
        CHECK(read_le32(bytes + 72 + index * sizeof(uint32_t)) == 0);
    }
    CHECK(bytes[20] == 0x11 && bytes[21] == 0x22 &&
          bytes[22] == 0x33 && bytes[23] == 0x44);

    memset(bytes, 0xA5, sizeof(bytes));
    memcpy(before, bytes, sizeof(bytes));
    state.range_adjust[0] = ACGC_GX_CANONICAL_FOG_RANGE_VALUE_MASK + 1;
    CHECK(!acgc_gx_canonical_fog_state_encode(
        &state, bytes, sizeof(bytes)));
    CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);

    fill_active_state(&state);
    memset(short_bytes, 0x5A, sizeof(short_bytes));
    memcpy(short_before, short_bytes, sizeof(short_bytes));
    CHECK(!acgc_gx_canonical_fog_state_encode(
        &state, short_bytes, sizeof(short_bytes)));
    CHECK(memcmp(short_bytes, short_before, sizeof(short_bytes)) == 0);
    CHECK(!acgc_gx_canonical_fog_state_encode(
        NULL, bytes, sizeof(bytes)));
    CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);
    CHECK(!acgc_gx_canonical_fog_state_encode(
        &state, NULL, sizeof(bytes)));
    return 1;
}

int main(void) {
    CHECK(accepts_canonical_fog_types());
    CHECK(accepts_denominator_degenerate_active_fog());
    CHECK(rejects_invalid_active_parameters());
    CHECK(checks_range_and_reserved_contract());
    CHECK(preserves_inactive_disabled_parameters());
    CHECK(encodes_all_fog_words_and_preserves_output());
    printf("GX canonical fog state tests: PASS\n");
    return 0;
}
