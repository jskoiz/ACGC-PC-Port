#include "acgc/gx_canonical_transform_state.h"

#include <string.h>

static int canonical_transform_binary32_is_finite(uint32_t bits) {
    return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

static int canonical_transform_words_are_zero(
    const uint32_t* words,
    uint32_t count
) {
    uint32_t index;

    for (index = 0; index < count; index++) {
        if (words[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int canonical_transform_words_are_finite(
    const uint32_t* words,
    uint32_t count
) {
    uint32_t index;

    for (index = 0; index < count; index++) {
        if (!canonical_transform_binary32_is_finite(words[index])) {
            return 0;
        }
    }
    return 1;
}

static int canonical_transform_projection_type_is_valid(uint32_t type) {
    return type == ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE ||
        type == ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_ORTHOGRAPHIC;
}

/* Do not divide or floor malformed GX matrix IDs. */
static int canonical_transform_logical_id_to_slot(uint32_t id) {
    uint32_t slot;

    for (slot = 0;
         slot < ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT;
         slot++) {
        if (id == slot *
                ACGC_GX_CANONICAL_TRANSFORM_LOGICAL_POSITION_ID_STRIDE) {
            return (int)slot;
        }
    }
    return -1;
}

static int canonical_transform_records_are_valid(
    const AcgcGxCanonicalTransformState* state
) {
    uint32_t slot;

    for (slot = 0;
         slot < ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT;
         slot++) {
        const uint32_t position_mask =
            ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(slot);
        const uint32_t normal_mask =
            ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(slot);

        if ((state->known_mask & position_mask) != 0) {
            if (!canonical_transform_words_are_finite(
                    state->position[slot],
                    ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT)) {
                return 0;
            }
        } else if (!canonical_transform_words_are_zero(
                       state->position[slot],
                       ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT)) {
            return 0;
        }

        if ((state->known_mask & normal_mask) != 0) {
            if (!canonical_transform_words_are_finite(
                    state->normal[slot],
                    ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT)) {
                return 0;
            }
        } else if (!canonical_transform_words_are_zero(
                       state->normal[slot],
                       ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT)) {
            return 0;
        }
    }
    return 1;
}

int acgc_gx_canonical_transform_state_validate(
    const AcgcGxCanonicalTransformState* state
) {
    int current_slot = -1;

    if (state == NULL ||
        (state->known_mask & ACGC_GX_CANONICAL_TRANSFORM_RESERVED_MASK) != 0 ||
        !canonical_transform_words_are_zero(
            state->reserved,
            ACGC_GX_CANONICAL_TRANSFORM_RESERVED_WORD_COUNT)) {
        return 0;
    }

    if ((state->known_mask &
            ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK) != 0) {
        if (!canonical_transform_projection_type_is_valid(
                state->projection_type) ||
            !canonical_transform_words_are_finite(
                state->projection,
                ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_WORD_COUNT)) {
            return 0;
        }
    } else if (state->projection_type != 0 ||
               !canonical_transform_words_are_zero(
                   state->projection,
                   ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_WORD_COUNT)) {
        return 0;
    }

    if ((state->known_mask &
            ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK) != 0) {
        current_slot = canonical_transform_logical_id_to_slot(
            state->current_position_id);
        if (current_slot < 0 ||
            (state->known_mask &
                ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(
                    (uint32_t)current_slot)) == 0) {
            return 0;
        }
    } else if (state->current_position_id != 0) {
        return 0;
    }

    if (!canonical_transform_records_are_valid(state)) {
        return 0;
    }
    return 1;
}

static int canonical_transform_entry_is_absent(
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry
) {
    return entry != NULL &&
        entry->section_id == ACGC_GX_CANONICAL_TRANSFORM_SECTION_ID &&
        entry->section_version == 0 &&
        entry->byte_offset == 0 &&
        entry->byte_size == 0 &&
        entry->count == 0 &&
        entry->capacity == 0 &&
        entry->valid_mask == 0 &&
        entry->reserved == 0;
}

int acgc_gx_canonical_transform_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
) {
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    if (!acgc_gx_canonical_envelope_validate(
            envelope, envelope_byte_size)) {
        return 0;
    }

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_ID - 1];
    if ((envelope->header.present_state_mask &
            ACGC_GX_CANONICAL_TRANSFORM_SECTION_MASK) == 0) {
        return canonical_transform_entry_is_absent(entry);
    }

    return entry->section_id == ACGC_GX_CANONICAL_TRANSFORM_SECTION_ID &&
        entry->section_version ==
            ACGC_GX_CANONICAL_TRANSFORM_SECTION_VERSION &&
        entry->byte_size == ACGC_GX_CANONICAL_TRANSFORM_SECTION_BYTE_SIZE &&
        entry->count == ACGC_GX_CANONICAL_TRANSFORM_SECTION_COUNT &&
        entry->capacity == ACGC_GX_CANONICAL_TRANSFORM_SECTION_CAPACITY &&
        entry->valid_mask == ACGC_GX_CANONICAL_TRANSFORM_SECTION_MASK &&
        entry->reserved == 0;
}

static void canonical_transform_write_le32(
    uint8_t* destination,
    uint32_t value
) {
    destination[0] = (uint8_t)(value & UINT32_C(0xFF));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    destination[3] = (uint8_t)((value >> 24) & UINT32_C(0xFF));
}

static void canonical_transform_encode_word(
    uint8_t* destination,
    size_t* offset,
    uint32_t value
) {
    canonical_transform_write_le32(destination + *offset, value);
    *offset += sizeof(uint32_t);
}

int acgc_gx_canonical_transform_state_encode(
    const AcgcGxCanonicalTransformState* state,
    uint8_t* destination,
    size_t destination_byte_size
) {
    uint8_t encoded[ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE];
    size_t offset = 0;
    uint32_t slot;
    uint32_t word;

    if (state == NULL || destination == NULL ||
        destination_byte_size != ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE ||
        !acgc_gx_canonical_transform_state_validate(state)) {
        return 0;
    }

    memset(encoded, 0, sizeof(encoded));
    canonical_transform_encode_word(
        encoded, &offset, state->projection_type);
    for (word = 0;
         word < ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_WORD_COUNT;
         word++) {
        canonical_transform_encode_word(
            encoded, &offset, state->projection[word]);
    }
    canonical_transform_encode_word(encoded, &offset, state->known_mask);
    canonical_transform_encode_word(
        encoded, &offset, state->current_position_id);
    for (word = 0;
         word < ACGC_GX_CANONICAL_TRANSFORM_RESERVED_WORD_COUNT;
         word++) {
        canonical_transform_encode_word(encoded, &offset, state->reserved[word]);
    }
    for (slot = 0;
         slot < ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT;
         slot++) {
        for (word = 0;
             word < ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT;
             word++) {
            canonical_transform_encode_word(
                encoded, &offset, state->position[slot][word]);
        }
    }
    for (slot = 0;
         slot < ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT;
         slot++) {
        for (word = 0;
             word < ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT;
             word++) {
            canonical_transform_encode_word(
                encoded, &offset, state->normal[slot][word]);
        }
    }

    if (offset != ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE) {
        return 0;
    }
    memcpy(destination, encoded, sizeof(encoded));
    return 1;
}
