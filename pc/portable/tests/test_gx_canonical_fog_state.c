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

int main(void) {
    CHECK(accepts_canonical_fog_types());
    CHECK(accepts_denominator_degenerate_active_fog());
    CHECK(rejects_invalid_active_parameters());
    CHECK(checks_range_and_reserved_contract());
    CHECK(preserves_inactive_disabled_parameters());
    printf("GX canonical fog state tests: PASS\n");
    return 0;
}
