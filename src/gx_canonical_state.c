#include "acgc/gx_canonical_state.h"

static int canonical_fog_type_is_valid(uint32_t fog_type) {
    switch (fog_type) {
        case ACGC_GX_CANONICAL_FOG_TYPE_NONE:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_EXP:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_EXP2:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_REVEXP:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_REVEXP2:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_LIN:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_EXP:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_EXP2:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_REVEXP:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_REVEXP2:
            return 1;
        default:
            return 0;
    }
}

static int binary32_is_finite(uint32_t bits) {
    return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

/* Compare finite IEEE-754 binary32 values without converting through host
 * floating-point types.  This keeps validation defined by the wire bits. */
static int binary32_less(uint32_t lhs, uint32_t rhs) {
    const uint32_t lhs_magnitude = lhs & UINT32_C(0x7FFFFFFF);
    const uint32_t rhs_magnitude = rhs & UINT32_C(0x7FFFFFFF);
    const int lhs_negative = (lhs & UINT32_C(0x80000000)) != 0;
    const int rhs_negative = (rhs & UINT32_C(0x80000000)) != 0;

    if (lhs_magnitude == 0 && rhs_magnitude == 0) {
        return 0;
    }
    if (lhs_negative != rhs_negative) {
        return lhs_negative;
    }
    if (lhs_negative) {
        return lhs_magnitude > rhs_magnitude;
    }
    return lhs_magnitude < rhs_magnitude;
}

static int canonical_reserved_words_are_zero(
    const AcgcGxCanonicalFogState* state
) {
    uint32_t index;

    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RESERVED_WORD_COUNT; index++) {
        if (state->reserved[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int canonical_range_words_are_valid(
    const AcgcGxCanonicalFogState* state
) {
    uint32_t index;

    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        if ((state->range_adjust[index] &
             ~ACGC_GX_CANONICAL_FOG_RANGE_VALUE_MASK) != 0) {
            return 0;
        }
    }
    return 1;
}

int acgc_gx_canonical_fog_state_validate(
    const AcgcGxCanonicalFogState* state
) {
    const int fog_is_active = state != NULL &&
        state->fog_type != ACGC_GX_CANONICAL_FOG_TYPE_NONE;
    const int fog_parameters_are_finite = state != NULL &&
        binary32_is_finite(state->start_bits) &&
        binary32_is_finite(state->end_bits) &&
        binary32_is_finite(state->near_bits) &&
        binary32_is_finite(state->far_bits);

    if (state == NULL ||
        !canonical_fog_type_is_valid(state->fog_type) ||
        state->range_adjust_enable > 1 ||
        state->range_center > ACGC_GX_CANONICAL_FOG_CENTER_SOURCE_MAX ||
        !canonical_range_words_are_valid(state) ||
        !canonical_reserved_words_are_zero(state)) {
        return 0;
    }

    /*
     * The decomp packs center + 342 into a ten-bit field.  A disabled range
     * adjustment retains the caller's widened u16 center without applying
     * that active-register limit; the range words remain structurally
     * bounded even while inactive.
     */
    if (state->range_adjust_enable != 0 &&
        state->range_center > ACGC_GX_CANONICAL_FOG_CENTER_MAX) {
        return 0;
    }

    /* GX_FOG_NONE disables fog math.  Its parameter words are intentionally
     * inactive and are not rewritten or interpreted by this validator. */
    if (fog_is_active && !fog_parameters_are_finite) {
        return 0;
    }

    /* The upstream GXSetFog assertions apply whenever these finite values
     * are supplied, including a disabled state. */
    if (binary32_is_finite(state->far_bits) &&
        binary32_less(state->far_bits, UINT32_C(0))) {
        return 0;
    }
    if (binary32_is_finite(state->far_bits) &&
        binary32_is_finite(state->near_bits) &&
        binary32_less(state->far_bits, state->near_bits)) {
        return 0;
    }

    /*
     * Do not reject far == near or end == start.  The decomp GXSetFog path
     * deliberately uses A=0, B=0.5, C=0 for either denominator-degenerate
     * case; a later renderer may apply that fallback without mutating this
     * value-only state.
     */
    return 1;
}
