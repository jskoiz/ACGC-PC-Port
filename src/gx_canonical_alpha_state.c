#include "acgc/gx_canonical_alpha_state.h"

static int canonical_alpha_word_is_bounded(
    uint32_t value,
    uint32_t minimum,
    uint32_t maximum
) {
    return value >= minimum && value <= maximum;
}

int acgc_gx_canonical_alpha_state_validate(
    const AcgcGxCanonicalAlphaState* state
) {
    if (state == NULL ||
        !canonical_alpha_word_is_bounded(
            state->comp0,
            ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN,
            ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX) ||
        !canonical_alpha_word_is_bounded(
            state->ref0,
            ACGC_GX_CANONICAL_ALPHA_REFERENCE_MIN,
            ACGC_GX_CANONICAL_ALPHA_REFERENCE_MAX) ||
        !canonical_alpha_word_is_bounded(
            state->op,
            ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN,
            ACGC_GX_CANONICAL_ALPHA_OPERATOR_MAX) ||
        !canonical_alpha_word_is_bounded(
            state->comp1,
            ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN,
            ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX) ||
        !canonical_alpha_word_is_bounded(
            state->ref1,
            ACGC_GX_CANONICAL_ALPHA_REFERENCE_MIN,
            ACGC_GX_CANONICAL_ALPHA_REFERENCE_MAX) ||
        !canonical_alpha_word_is_bounded(
            state->color_update_enable,
            ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX) ||
        !canonical_alpha_word_is_bounded(
            state->alpha_update_enable,
            ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX) ||
        !canonical_alpha_word_is_bounded(
            state->z_comp_loc_before_tex,
            ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX)) {
        return 0;
    }
    return 1;
}

static int canonical_alpha_entry_is_absent(
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry
) {
    return entry != NULL &&
        entry->section_id == ACGC_GX_CANONICAL_ALPHA_SECTION_ID &&
        entry->section_version == 0 &&
        entry->byte_offset == 0 &&
        entry->byte_size == 0 &&
        entry->count == 0 &&
        entry->capacity == 0 &&
        entry->valid_mask == 0 &&
        entry->reserved == 0;
}

int acgc_gx_canonical_alpha_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
) {
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    if (!acgc_gx_canonical_envelope_validate(
            envelope, envelope_byte_size)) {
        return 0;
    }

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_ALPHA_SECTION_ID - 1];
    if ((envelope->header.present_state_mask &
            ACGC_GX_CANONICAL_ALPHA_SECTION_MASK) == 0) {
        return canonical_alpha_entry_is_absent(entry);
    }

    return entry->section_id == ACGC_GX_CANONICAL_ALPHA_SECTION_ID &&
        entry->section_version == ACGC_GX_CANONICAL_ALPHA_SECTION_VERSION &&
        entry->byte_size == ACGC_GX_CANONICAL_ALPHA_SECTION_BYTE_SIZE &&
        entry->count == ACGC_GX_CANONICAL_ALPHA_SECTION_COUNT &&
        entry->capacity == ACGC_GX_CANONICAL_ALPHA_SECTION_CAPACITY &&
        entry->valid_mask == ACGC_GX_CANONICAL_ALPHA_SECTION_MASK &&
        entry->reserved == 0;
}
