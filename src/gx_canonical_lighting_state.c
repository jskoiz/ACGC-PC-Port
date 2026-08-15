#include "acgc/gx_canonical_lighting_state.h"

static int canonical_lighting_binary32_is_finite(uint32_t bits) {
    return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

static int canonical_lighting_reserved_words_are_zero(
    const AcgcGxCanonicalLightingRecord* record
) {
    uint32_t index;

    for (index = 0;
         index < ACGC_GX_CANONICAL_LIGHTING_RESERVED_WORD_COUNT;
         index++) {
        if (record->reserved[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int canonical_lighting_float_vector_is_finite(
    const uint32_t* words
) {
    uint32_t index;

    for (index = 0;
         index < ACGC_GX_CANONICAL_LIGHTING_FLOAT_VECTOR_COUNT;
         index++) {
        if (!canonical_lighting_binary32_is_finite(words[index])) {
            return 0;
        }
    }
    return 1;
}

static int canonical_lighting_record_is_valid(
    const AcgcGxCanonicalLightingRecord* record
) {
    return record != NULL &&
        canonical_lighting_reserved_words_are_zero(record) &&
        canonical_lighting_float_vector_is_finite(
            record->angular_attenuation) &&
        canonical_lighting_float_vector_is_finite(
            record->distance_attenuation) &&
        canonical_lighting_float_vector_is_finite(record->position) &&
        canonical_lighting_float_vector_is_finite(record->direction);
}

static int canonical_lighting_record_is_zero(
    const AcgcGxCanonicalLightingRecord* record
) {
    uint32_t index;

    if (record == NULL || record->color_rgba8 != 0 ||
        !canonical_lighting_reserved_words_are_zero(record)) {
        return 0;
    }

    for (index = 0;
         index < ACGC_GX_CANONICAL_LIGHTING_FLOAT_VECTOR_COUNT;
         index++) {
        if (record->angular_attenuation[index] != 0 ||
            record->distance_attenuation[index] != 0 ||
            record->position[index] != 0 ||
            record->direction[index] != 0) {
            return 0;
        }
    }
    return 1;
}

int acgc_gx_canonical_lighting_state_validate(
    const AcgcGxCanonicalLightingState* state
) {
    uint32_t slot;

    if (state == NULL ||
        state->loaded_mask < ACGC_GX_CANONICAL_LIGHTING_LOADED_MASK_MIN ||
        state->loaded_mask > ACGC_GX_CANONICAL_LIGHTING_LOADED_MASK_MAX) {
        return 0;
    }

    for (slot = 0;
         slot < ACGC_GX_CANONICAL_LIGHTING_SLOT_COUNT;
         slot++) {
        const AcgcGxCanonicalLightingRecord* record =
            &state->records[slot];
        const uint32_t slot_mask = UINT32_C(1) << slot;

        if ((state->loaded_mask & slot_mask) != 0) {
            if (!canonical_lighting_record_is_valid(record)) {
                return 0;
            }
        } else if (!canonical_lighting_record_is_zero(record)) {
            return 0;
        }
    }
    return 1;
}

static int canonical_lighting_entry_is_absent(
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry
) {
    return entry != NULL &&
        entry->section_id == ACGC_GX_CANONICAL_LIGHTING_SECTION_ID &&
        entry->section_version == 0 &&
        entry->byte_offset == 0 &&
        entry->byte_size == 0 &&
        entry->count == 0 &&
        entry->capacity == 0 &&
        entry->valid_mask == 0 &&
        entry->reserved == 0;
}

int acgc_gx_canonical_lighting_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
) {
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    if (!acgc_gx_canonical_envelope_validate(
            envelope, envelope_byte_size)) {
        return 0;
    }

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_LIGHTING_SECTION_ID - 1];
    if ((envelope->header.present_state_mask &
            ACGC_GX_CANONICAL_LIGHTING_SECTION_MASK) == 0) {
        return canonical_lighting_entry_is_absent(entry);
    }

    return entry->section_id == ACGC_GX_CANONICAL_LIGHTING_SECTION_ID &&
        entry->section_version == ACGC_GX_CANONICAL_LIGHTING_SECTION_VERSION &&
        entry->byte_size == ACGC_GX_CANONICAL_LIGHTING_SECTION_BYTE_SIZE &&
        entry->count == ACGC_GX_CANONICAL_LIGHTING_SECTION_COUNT &&
        entry->capacity == ACGC_GX_CANONICAL_LIGHTING_SECTION_CAPACITY &&
        entry->valid_mask == ACGC_GX_CANONICAL_LIGHTING_SECTION_MASK &&
        entry->reserved == 0;
}
