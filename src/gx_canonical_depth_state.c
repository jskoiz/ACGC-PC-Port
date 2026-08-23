#include "acgc/gx_canonical_depth_state.h"

#include <string.h>

static int canonical_depth_word_is_bounded(
    uint32_t value,
    uint32_t minimum,
    uint32_t maximum
) {
    return value >= minimum && value <= maximum;
}

int acgc_gx_canonical_depth_state_validate(
    const AcgcGxCanonicalDepthState* state
) {
    if (state == NULL ||
        !canonical_depth_word_is_bounded(
            state->z_compare_enable,
            ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MAX) ||
        !canonical_depth_word_is_bounded(
            state->z_compare_func,
            ACGC_GX_CANONICAL_DEPTH_COMPARE_MIN,
            ACGC_GX_CANONICAL_DEPTH_COMPARE_MAX) ||
        !canonical_depth_word_is_bounded(
            state->z_update_enable,
            ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MAX) ||
        state->reserved != 0) {
        return 0;
    }
    return 1;
}

static void canonical_depth_write_le32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)(value & UINT32_C(0xFF));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    destination[3] = (uint8_t)((value >> 24) & UINT32_C(0xFF));
}

static void canonical_depth_encode_word(
    uint8_t* destination,
    size_t* offset,
    uint32_t value
) {
    canonical_depth_write_le32(destination + *offset, value);
    *offset += sizeof(uint32_t);
}

int acgc_gx_canonical_depth_state_encode(
    const AcgcGxCanonicalDepthState* state,
    uint8_t* destination,
    size_t destination_byte_size
) {
    uint8_t encoded[ACGC_GX_CANONICAL_DEPTH_STATE_SIZE];
    size_t offset = 0;
    size_t index;

    if (state == NULL || destination == NULL ||
        destination_byte_size != ACGC_GX_CANONICAL_DEPTH_STATE_SIZE ||
        !acgc_gx_canonical_depth_state_validate(state)) {
        return 0;
    }

    memset(encoded, 0, sizeof(encoded));
    canonical_depth_encode_word(encoded, &offset, state->z_compare_enable);
    canonical_depth_encode_word(encoded, &offset, state->z_compare_func);
    canonical_depth_encode_word(encoded, &offset, state->z_update_enable);
    canonical_depth_encode_word(encoded, &offset, state->reserved);
    if (offset != ACGC_GX_CANONICAL_DEPTH_STATE_SIZE) {
        return 0;
    }
    for (index = 0; index < sizeof(encoded); index++) {
        destination[index] = encoded[index];
    }
    return 1;
}

static int canonical_depth_entry_is_absent(
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry
) {
    return entry != NULL &&
        entry->section_id == ACGC_GX_CANONICAL_DEPTH_SECTION_ID &&
        entry->section_version == 0 &&
        entry->byte_offset == 0 &&
        entry->byte_size == 0 &&
        entry->count == 0 &&
        entry->capacity == 0 &&
        entry->valid_mask == 0 &&
        entry->reserved == 0;
}

int acgc_gx_canonical_depth_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
) {
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    if (!acgc_gx_canonical_envelope_validate(
            envelope, envelope_byte_size)) {
        return 0;
    }

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_DEPTH_SECTION_ID - 1];
    if ((envelope->header.present_state_mask &
            ACGC_GX_CANONICAL_DEPTH_SECTION_MASK) == 0) {
        return canonical_depth_entry_is_absent(entry);
    }

    return entry->section_id == ACGC_GX_CANONICAL_DEPTH_SECTION_ID &&
        entry->section_version == ACGC_GX_CANONICAL_DEPTH_SECTION_VERSION &&
        entry->byte_size == ACGC_GX_CANONICAL_DEPTH_SECTION_BYTE_SIZE &&
        entry->count == ACGC_GX_CANONICAL_DEPTH_SECTION_COUNT &&
        entry->capacity == ACGC_GX_CANONICAL_DEPTH_SECTION_CAPACITY &&
        entry->valid_mask == ACGC_GX_CANONICAL_DEPTH_SECTION_MASK &&
        entry->reserved == 0;
}
