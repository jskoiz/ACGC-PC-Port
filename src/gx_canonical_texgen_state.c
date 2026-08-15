#include "acgc/gx_canonical_texgen_state.h"

#include <string.h>

static uint32_t canonical_texgen_popcount(uint32_t value) {
    uint32_t count = 0;

    while (value != 0) {
        count += value & UINT32_C(1);
        value >>= 1;
    }
    return count;
}

static int canonical_texgen_words_are_zero(
    const uint32_t* words,
    uint32_t count
) {
    uint32_t index;

    if (words == NULL) {
        return 0;
    }
    for (index = 0; index < count; index++) {
        if (words[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int canonical_texgen_binary32_is_finite(uint32_t bits) {
    return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

static int canonical_texgen_bool_is_valid(uint32_t value) {
    return value == 0 || value == 1;
}

static int canonical_texgen_function_is_regular(uint32_t function) {
    return function == ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX3X4 ||
        function == ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX2X4;
}

static int canonical_texgen_function_is_bump(uint32_t function) {
    return function >= ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP0 &&
        function <= ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP7;
}

static int canonical_texgen_function_is_valid(uint32_t function) {
    return canonical_texgen_function_is_regular(function) ||
        canonical_texgen_function_is_bump(function) ||
        function == ACGC_GX_CANONICAL_TEXGEN_FUNCTION_SRTG;
}

static int canonical_texgen_regular_source_is_valid(uint32_t source) {
    return source <= ACGC_GX_CANONICAL_TEXGEN_SOURCE_TANGENT ||
        (source >= ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX0 &&
         source <= ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX7) ||
        source == ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR0 ||
        source == ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR1;
}

static int canonical_texgen_source_is_valid(
    uint32_t function,
    uint32_t source
) {
    if (canonical_texgen_function_is_regular(function)) {
        return canonical_texgen_regular_source_is_valid(source);
    }
    if (canonical_texgen_function_is_bump(function)) {
        return source >= ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD0 &&
            source <= ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD6;
    }
    return function == ACGC_GX_CANONICAL_TEXGEN_FUNCTION_SRTG &&
        (source == ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR0 ||
         source == ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR1);
}

static int canonical_texgen_ordinary_slot(uint32_t logical_id) {
    uint32_t index;

    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT;
         index++) {
        if (logical_id ==
            ACGC_GX_CANONICAL_TEXGEN_ORDINARY_LOGICAL_ID(index)) {
            return (int)index;
        }
    }
    return -1;
}

static int canonical_texgen_post_slot(uint32_t logical_id) {
    uint32_t index;

    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT;
         index++) {
        if (logical_id == ACGC_GX_CANONICAL_TEXGEN_POST_LOGICAL_ID(index)) {
            return (int)index;
        }
    }
    return -1;
}

static int canonical_texgen_matrix_type_is_valid(
    int post,
    uint32_t type
) {
    if (post) {
        return type == ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4;
    }
    return type == ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4 ||
        type == ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX2X4;
}

static int canonical_texgen_matrix_record_is_valid(
    const AcgcGxCanonicalTexgenMatrixRecord* record,
    uint32_t expected_id,
    int post,
    int slot_known
) {
    uint32_t word;

    if (record == NULL || record->logical_id != expected_id ||
        (record->known_word_mask &
            ~ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4) != 0) {
        return 0;
    }

    for (word = 0;
         word < ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4;
         word++) {
        if ((record->known_word_mask & (UINT32_C(1) << word)) == 0 &&
            record->words[word] != 0) {
            return 0;
        }
        if ((record->known_word_mask & (UINT32_C(1) << word)) != 0 &&
            !canonical_texgen_binary32_is_finite(record->words[word])) {
            return 0;
        }
    }

    if (!slot_known) {
        return record->last_load_type == 0 &&
            record->last_written_word_count == 0 &&
            record->known_word_mask == 0 &&
            canonical_texgen_words_are_zero(record->words, 12);
    }

    /* A known slot with no surviving written range is a valid empty record. */
    if (record->last_written_word_count == 0) {
        return canonical_texgen_matrix_type_is_valid(
                post, record->last_load_type) &&
            record->known_word_mask == 0 &&
            canonical_texgen_words_are_zero(record->words, 12);
    }

    if (record->last_written_word_count !=
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_2X4 &&
        record->last_written_word_count !=
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4) {
        return 0;
    }
    if (post && record->last_written_word_count !=
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4) {
        return 0;
    }
    if (!canonical_texgen_matrix_type_is_valid(
            post, record->last_load_type)) {
        return 0;
    }
    if (record->last_load_type ==
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX2X4 &&
        record->last_written_word_count !=
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_2X4) {
        return 0;
    }
    if (record->last_load_type ==
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4 &&
        record->last_written_word_count !=
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4) {
        return 0;
    }
    return 1;
}

static int canonical_texgen_matrix_range_is_known(
    const AcgcGxCanonicalTexgenMatrixRecord* record,
    uint32_t word_count
) {
    const uint32_t required_mask = word_count == 8 ?
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_2X4 :
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4;

    return record != NULL &&
        (record->known_word_mask & required_mask) == required_mask;
}

static int canonical_texgen_record_is_valid(
    const AcgcGxCanonicalTexgenRecord* record
) {
    if (record == NULL ||
        (record->component_known &
            ~ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL) != 0) {
        return 0;
    }
    if (record->component_known == 0) {
        return record->function == 0 && record->source == 0 &&
            record->ordinary_matrix_id == 0 && record->normalize == 0 &&
            record->post_matrix_id == 0 && record->reserved[0] == 0 &&
            record->reserved[1] == 0;
    }

    /* GXSetTexCoordGen2 establishes the complete record atomically. */
    if (record->component_known !=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL ||
        record->reserved[0] != 0 || record->reserved[1] != 0 ||
        !canonical_texgen_function_is_valid(record->function) ||
        !canonical_texgen_source_is_valid(record->function, record->source) ||
        canonical_texgen_ordinary_slot(record->ordinary_matrix_id) < 0 ||
        canonical_texgen_post_slot(record->post_matrix_id) < 0 ||
        !canonical_texgen_bool_is_valid(record->normalize)) {
        return 0;
    }
    return 1;
}

static int canonical_texgen_su_record_is_valid(
    const AcgcGxCanonicalTexgenSuRecord* record
) {
    const uint32_t known = record == NULL ? UINT32_MAX : record->component_known;

    if (record == NULL ||
        (known & ~ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_ALL) != 0) {
        return 0;
    }
    if ((known & ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_MANUAL) == 0 &&
        record->manual_enable != 0) {
        return 0;
    }
    if ((known & ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_SCALE_S) == 0 &&
        record->scale_s_raw_u16 != 0) {
        return 0;
    }
    if ((known & ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_SCALE_T) == 0 &&
        record->scale_t_raw_u16 != 0) {
        return 0;
    }
    if ((known & ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_BIAS_S) == 0 &&
        record->bias_s != 0) {
        return 0;
    }
    if ((known & ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_BIAS_T) == 0 &&
        record->bias_t != 0) {
        return 0;
    }
    if ((known & ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_CYLINDER_S) == 0 &&
        record->cylinder_s != 0) {
        return 0;
    }
    if ((known & ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_CYLINDER_T) == 0 &&
        record->cylinder_t != 0) {
        return 0;
    }
    if (record->scale_s_raw_u16 > UINT32_C(0xFFFF) ||
        record->scale_t_raw_u16 > UINT32_C(0xFFFF)) {
        return 0;
    }
    return ((known & ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_MANUAL) == 0 ||
            canonical_texgen_bool_is_valid(record->manual_enable)) &&
        ((known & ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_BIAS_S) == 0 ||
         canonical_texgen_bool_is_valid(record->bias_s)) &&
        ((known & ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_BIAS_T) == 0 ||
         canonical_texgen_bool_is_valid(record->bias_t)) &&
        ((known & ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_CYLINDER_S) == 0 ||
         canonical_texgen_bool_is_valid(record->cylinder_s)) &&
        ((known & ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_CYLINDER_T) == 0 ||
         canonical_texgen_bool_is_valid(record->cylinder_t));
}

static int canonical_texgen_records_are_complete(
    const AcgcGxCanonicalTexgenState* state
) {
    uint32_t index;

    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_COUNT; index++) {
        if (state->texgen[index].component_known !=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL) {
            return 0;
        }
    }
    return 1;
}

static int canonical_texgen_matrix_family_is_complete(
    const AcgcGxCanonicalTexgenMatrixRecord* records,
    uint32_t count,
    uint32_t known_mask
) {
    uint32_t index;

    if (known_mask != ((UINT32_C(1) << count) - 1u)) {
        return 0;
    }
    for (index = 0; index < count; index++) {
        if (!canonical_texgen_matrix_range_is_known(&records[index], 12)) {
            return 0;
        }
    }
    return 1;
}

static int canonical_texgen_su_family_is_complete(
    const AcgcGxCanonicalTexgenState* state
) {
    uint32_t index;

    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_SU_COUNT; index++) {
        if (state->su[index].component_known !=
            ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_ALL) {
            return 0;
        }
    }
    return 1;
}

static uint32_t canonical_texgen_expected_component_summary(
    const AcgcGxCanonicalTexgenState* state
) {
    uint32_t summary = 0;

    if (canonical_texgen_records_are_complete(state)) {
        summary |= ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_TEXGEN;
    }
    if (canonical_texgen_matrix_family_is_complete(
            state->ordinary_matrix,
            ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT,
            state->header.ordinary_matrix_known_mask)) {
        summary |=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_ORDINARY_MATRIX;
    }
    if (canonical_texgen_matrix_family_is_complete(
            state->post_matrix,
            ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT,
            state->header.post_matrix_known_mask)) {
        summary |= ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_POST_MATRIX;
    }
    if (canonical_texgen_su_family_is_complete(state)) {
        summary |= ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_SU;
    }
    return summary;
}

static int canonical_texgen_entry_is_absent(
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry
) {
    return entry != NULL &&
        entry->section_id == ACGC_GX_CANONICAL_TEXGEN_SECTION_ID &&
        entry->section_version == 0 && entry->byte_offset == 0 &&
        entry->byte_size == 0 && entry->count == 0 && entry->capacity == 0 &&
        entry->valid_mask == 0 && entry->reserved == 0;
}

int acgc_gx_canonical_texgen_state_validate(
    const AcgcGxCanonicalTexgenState* state
) {
    uint32_t index;
    uint32_t phase = 0;
    uint32_t bump_count = 0;
    uint32_t color_count = 0;
    uint32_t color_mask = 0;

    if (state == NULL) {
        return 0;
    }
    if (state->header.active_texgen_count >
            ACGC_GX_CANONICAL_TEXGEN_CAPACITY ||
        state->header.texgen_capacity !=
            ACGC_GX_CANONICAL_TEXGEN_CAPACITY ||
        state->header.ordinary_matrix_capacity !=
            ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_CAPACITY ||
        state->header.post_matrix_capacity !=
            ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_CAPACITY ||
        state->header.su_capacity != ACGC_GX_CANONICAL_TEXGEN_SU_CAPACITY ||
        (state->header.texgen_known_mask &
            ~ACGC_GX_CANONICAL_TEXGEN_KNOWN_MASK) != 0 ||
        (state->header.ordinary_matrix_known_mask &
            ~ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_KNOWN_MASK) != 0 ||
        (state->header.post_matrix_known_mask &
            ~ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_KNOWN_MASK) != 0 ||
        (state->header.su_known_mask &
            ~ACGC_GX_CANONICAL_TEXGEN_SU_KNOWN_MASK) != 0 ||
        (state->header.component_known_summary &
            ~ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_MASK) != 0 ||
        state->header.reserved[0] != 0 || state->header.reserved[1] != 0) {
        return 0;
    }
    if (state->header.known_texgen_count !=
            canonical_texgen_popcount(state->header.texgen_known_mask) ||
        state->header.ordinary_matrix_count !=
            canonical_texgen_popcount(
                state->header.ordinary_matrix_known_mask) ||
        state->header.post_matrix_count !=
            canonical_texgen_popcount(state->header.post_matrix_known_mask) ||
        state->header.su_count !=
            canonical_texgen_popcount(state->header.su_known_mask)) {
        return 0;
    }
    if ((state->header.texgen_known_mask &
            ((UINT32_C(1) << state->header.active_texgen_count) - 1u)) !=
        ((UINT32_C(1) << state->header.active_texgen_count) - 1u)) {
        return 0;
    }

    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_COUNT; index++) {
        const AcgcGxCanonicalTexgenRecord* record = &state->texgen[index];
        const int record_known =
            (state->header.texgen_known_mask & (UINT32_C(1) << index)) != 0;

        if (!canonical_texgen_record_is_valid(record) ||
            record_known != (record->component_known != 0)) {
            return 0;
        }
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT;
         index++) {
        if (!canonical_texgen_matrix_record_is_valid(
                &state->ordinary_matrix[index],
                ACGC_GX_CANONICAL_TEXGEN_ORDINARY_LOGICAL_ID(index), 0,
                (state->header.ordinary_matrix_known_mask &
                    (UINT32_C(1) << index)) != 0)) {
            return 0;
        }
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT;
         index++) {
        if (!canonical_texgen_matrix_record_is_valid(
                &state->post_matrix[index],
                ACGC_GX_CANONICAL_TEXGEN_POST_LOGICAL_ID(index), 1,
                (state->header.post_matrix_known_mask &
                    (UINT32_C(1) << index)) != 0)) {
            return 0;
        }
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_SU_COUNT; index++) {
        const int record_known =
            (state->header.su_known_mask & (UINT32_C(1) << index)) != 0;

        if (!canonical_texgen_su_record_is_valid(&state->su[index]) ||
            record_known != (state->su[index].component_known != 0)) {
            return 0;
        }
    }

    for (index = 0; index < state->header.active_texgen_count; index++) {
        const AcgcGxCanonicalTexgenRecord* record = &state->texgen[index];
        const int ordinary_slot =
            canonical_texgen_ordinary_slot(record->ordinary_matrix_id);
        const int post_slot =
            canonical_texgen_post_slot(record->post_matrix_id);
        uint32_t ordinary_word_count = 0;

        if (canonical_texgen_function_is_regular(record->function)) {
            if (phase != 0) {
                return 0;
            }
            ordinary_word_count = record->function ==
                ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX2X4 ? 8u : 12u;
        } else if (canonical_texgen_function_is_bump(record->function)) {
            uint32_t source_index;

            if (phase > 1 || ++bump_count > 3) {
                return 0;
            }
            source_index = record->source -
                ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD0;
            if (source_index >= index ||
                !canonical_texgen_function_is_regular(
                    state->texgen[source_index].function)) {
                return 0;
            }
            phase = 1;
        } else {
            if (phase > 2 || ++color_count > 2) {
                return 0;
            }
            phase = 2;
            if (record->source == ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR0) {
                if (color_count != 1 || (color_mask & UINT32_C(1)) != 0) {
                    return 0;
                }
                color_mask |= UINT32_C(1);
            } else {
                if (color_count == 1 || (color_mask & UINT32_C(2)) != 0) {
                    return 0;
                }
                color_mask |= UINT32_C(2);
            }
        }

        if (ordinary_slot < 0 || post_slot < 0 ||
            (ordinary_word_count != 0 &&
             !canonical_texgen_matrix_range_is_known(
                 &state->ordinary_matrix[ordinary_slot], ordinary_word_count)) ||
            !canonical_texgen_matrix_range_is_known(
                &state->post_matrix[post_slot], 12)) {
            return 0;
        }
    }

    return state->header.component_known_summary ==
        canonical_texgen_expected_component_summary(state);
}

int acgc_gx_canonical_texgen_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
) {
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    if (!acgc_gx_canonical_envelope_validate(
            envelope, envelope_byte_size)) {
        return 0;
    }
    entry = &envelope->directory[
        ACGC_GX_CANONICAL_TEXGEN_SECTION_ID - 1];
    if ((envelope->header.present_state_mask &
            ACGC_GX_CANONICAL_TEXGEN_SECTION_MASK) == 0) {
        return canonical_texgen_entry_is_absent(entry);
    }
    return entry->section_id == ACGC_GX_CANONICAL_TEXGEN_SECTION_ID &&
        entry->section_version == ACGC_GX_CANONICAL_TEXGEN_SECTION_VERSION &&
        entry->byte_size == ACGC_GX_CANONICAL_TEXGEN_SECTION_BYTE_SIZE &&
        entry->count == ACGC_GX_CANONICAL_TEXGEN_SECTION_COUNT &&
        entry->capacity == ACGC_GX_CANONICAL_TEXGEN_SECTION_CAPACITY &&
        entry->valid_mask == ACGC_GX_CANONICAL_TEXGEN_SECTION_MASK &&
        entry->reserved == 0;
}

static void canonical_texgen_write_le32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)(value & UINT32_C(0xFF));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    destination[3] = (uint8_t)((value >> 24) & UINT32_C(0xFF));
}

static uint32_t canonical_texgen_read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static void canonical_texgen_encode_word(
    uint8_t* destination,
    size_t* offset,
    uint32_t value
) {
    canonical_texgen_write_le32(destination + *offset, value);
    *offset += sizeof(uint32_t);
}

static uint32_t canonical_texgen_decode_word(
    const uint8_t* source,
    size_t* offset
) {
    const uint32_t value = canonical_texgen_read_le32(source + *offset);

    *offset += sizeof(uint32_t);
    return value;
}

int acgc_gx_canonical_texgen_state_encode(
    const AcgcGxCanonicalTexgenState* state,
    uint8_t* destination,
    size_t destination_byte_size
) {
    uint8_t encoded[ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE];
    size_t offset = 0;
    uint32_t index;
    uint32_t word;

    if (state == NULL || destination == NULL ||
        destination_byte_size != ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE ||
        !acgc_gx_canonical_texgen_state_validate(state)) {
        return 0;
    }

    memset(encoded, 0, sizeof(encoded));
    canonical_texgen_encode_word(
        encoded, &offset, state->header.active_texgen_count);
    canonical_texgen_encode_word(encoded, &offset, state->header.texgen_capacity);
    canonical_texgen_encode_word(
        encoded, &offset, state->header.known_texgen_count);
    canonical_texgen_encode_word(
        encoded, &offset, state->header.ordinary_matrix_count);
    canonical_texgen_encode_word(
        encoded, &offset, state->header.ordinary_matrix_capacity);
    canonical_texgen_encode_word(
        encoded, &offset, state->header.post_matrix_count);
    canonical_texgen_encode_word(
        encoded, &offset, state->header.post_matrix_capacity);
    canonical_texgen_encode_word(encoded, &offset, state->header.su_count);
    canonical_texgen_encode_word(encoded, &offset, state->header.su_capacity);
    canonical_texgen_encode_word(
        encoded, &offset, state->header.texgen_known_mask);
    canonical_texgen_encode_word(
        encoded, &offset, state->header.ordinary_matrix_known_mask);
    canonical_texgen_encode_word(
        encoded, &offset, state->header.post_matrix_known_mask);
    canonical_texgen_encode_word(encoded, &offset, state->header.su_known_mask);
    canonical_texgen_encode_word(
        encoded, &offset, state->header.component_known_summary);
    canonical_texgen_encode_word(encoded, &offset, state->header.reserved[0]);
    canonical_texgen_encode_word(encoded, &offset, state->header.reserved[1]);

    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_COUNT; index++) {
        const AcgcGxCanonicalTexgenRecord* record = &state->texgen[index];

        canonical_texgen_encode_word(encoded, &offset, record->function);
        canonical_texgen_encode_word(encoded, &offset, record->source);
        canonical_texgen_encode_word(
            encoded, &offset, record->ordinary_matrix_id);
        canonical_texgen_encode_word(encoded, &offset, record->normalize);
        canonical_texgen_encode_word(encoded, &offset, record->post_matrix_id);
        canonical_texgen_encode_word(encoded, &offset, record->component_known);
        canonical_texgen_encode_word(encoded, &offset, record->reserved[0]);
        canonical_texgen_encode_word(encoded, &offset, record->reserved[1]);
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT;
         index++) {
        const AcgcGxCanonicalTexgenMatrixRecord* record =
            &state->ordinary_matrix[index];

        canonical_texgen_encode_word(encoded, &offset, record->logical_id);
        canonical_texgen_encode_word(encoded, &offset, record->last_load_type);
        canonical_texgen_encode_word(
            encoded, &offset, record->last_written_word_count);
        canonical_texgen_encode_word(encoded, &offset, record->known_word_mask);
        for (word = 0; word < 12; word++) {
            canonical_texgen_encode_word(encoded, &offset, record->words[word]);
        }
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT;
         index++) {
        const AcgcGxCanonicalTexgenMatrixRecord* record =
            &state->post_matrix[index];

        canonical_texgen_encode_word(encoded, &offset, record->logical_id);
        canonical_texgen_encode_word(encoded, &offset, record->last_load_type);
        canonical_texgen_encode_word(
            encoded, &offset, record->last_written_word_count);
        canonical_texgen_encode_word(encoded, &offset, record->known_word_mask);
        for (word = 0; word < 12; word++) {
            canonical_texgen_encode_word(encoded, &offset, record->words[word]);
        }
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_SU_COUNT; index++) {
        const AcgcGxCanonicalTexgenSuRecord* record = &state->su[index];

        canonical_texgen_encode_word(encoded, &offset, record->component_known);
        canonical_texgen_encode_word(encoded, &offset, record->manual_enable);
        canonical_texgen_encode_word(
            encoded, &offset, record->scale_s_raw_u16);
        canonical_texgen_encode_word(
            encoded, &offset, record->scale_t_raw_u16);
        canonical_texgen_encode_word(encoded, &offset, record->bias_s);
        canonical_texgen_encode_word(encoded, &offset, record->bias_t);
        canonical_texgen_encode_word(encoded, &offset, record->cylinder_s);
        canonical_texgen_encode_word(encoded, &offset, record->cylinder_t);
    }

    if (offset != ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE) {
        return 0;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE; index++) {
        destination[index] = encoded[index];
    }
    return 1;
}

int acgc_gx_canonical_texgen_state_decode(
    const uint8_t* source,
    size_t source_byte_size,
    AcgcGxCanonicalTexgenState* destination
) {
    AcgcGxCanonicalTexgenState decoded;
    size_t offset = 0;
    uint32_t index;
    uint32_t word;

    if (source == NULL || destination == NULL ||
        source_byte_size != ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE) {
        return 0;
    }
    memset(&decoded, 0, sizeof(decoded));
    decoded.header.active_texgen_count =
        canonical_texgen_decode_word(source, &offset);
    decoded.header.texgen_capacity =
        canonical_texgen_decode_word(source, &offset);
    decoded.header.known_texgen_count =
        canonical_texgen_decode_word(source, &offset);
    decoded.header.ordinary_matrix_count =
        canonical_texgen_decode_word(source, &offset);
    decoded.header.ordinary_matrix_capacity =
        canonical_texgen_decode_word(source, &offset);
    decoded.header.post_matrix_count =
        canonical_texgen_decode_word(source, &offset);
    decoded.header.post_matrix_capacity =
        canonical_texgen_decode_word(source, &offset);
    decoded.header.su_count = canonical_texgen_decode_word(source, &offset);
    decoded.header.su_capacity = canonical_texgen_decode_word(source, &offset);
    decoded.header.texgen_known_mask =
        canonical_texgen_decode_word(source, &offset);
    decoded.header.ordinary_matrix_known_mask =
        canonical_texgen_decode_word(source, &offset);
    decoded.header.post_matrix_known_mask =
        canonical_texgen_decode_word(source, &offset);
    decoded.header.su_known_mask = canonical_texgen_decode_word(source, &offset);
    decoded.header.component_known_summary =
        canonical_texgen_decode_word(source, &offset);
    decoded.header.reserved[0] = canonical_texgen_decode_word(source, &offset);
    decoded.header.reserved[1] = canonical_texgen_decode_word(source, &offset);

    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_COUNT; index++) {
        AcgcGxCanonicalTexgenRecord* record = &decoded.texgen[index];

        record->function = canonical_texgen_decode_word(source, &offset);
        record->source = canonical_texgen_decode_word(source, &offset);
        record->ordinary_matrix_id =
            canonical_texgen_decode_word(source, &offset);
        record->normalize = canonical_texgen_decode_word(source, &offset);
        record->post_matrix_id = canonical_texgen_decode_word(source, &offset);
        record->component_known =
            canonical_texgen_decode_word(source, &offset);
        record->reserved[0] = canonical_texgen_decode_word(source, &offset);
        record->reserved[1] = canonical_texgen_decode_word(source, &offset);
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT;
         index++) {
        AcgcGxCanonicalTexgenMatrixRecord* record =
            &decoded.ordinary_matrix[index];

        record->logical_id = canonical_texgen_decode_word(source, &offset);
        record->last_load_type = canonical_texgen_decode_word(source, &offset);
        record->last_written_word_count =
            canonical_texgen_decode_word(source, &offset);
        record->known_word_mask = canonical_texgen_decode_word(source, &offset);
        for (word = 0; word < 12; word++) {
            record->words[word] = canonical_texgen_decode_word(source, &offset);
        }
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT;
         index++) {
        AcgcGxCanonicalTexgenMatrixRecord* record =
            &decoded.post_matrix[index];

        record->logical_id = canonical_texgen_decode_word(source, &offset);
        record->last_load_type = canonical_texgen_decode_word(source, &offset);
        record->last_written_word_count =
            canonical_texgen_decode_word(source, &offset);
        record->known_word_mask = canonical_texgen_decode_word(source, &offset);
        for (word = 0; word < 12; word++) {
            record->words[word] = canonical_texgen_decode_word(source, &offset);
        }
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_SU_COUNT; index++) {
        AcgcGxCanonicalTexgenSuRecord* record = &decoded.su[index];

        record->component_known = canonical_texgen_decode_word(source, &offset);
        record->manual_enable = canonical_texgen_decode_word(source, &offset);
        record->scale_s_raw_u16 =
            canonical_texgen_decode_word(source, &offset);
        record->scale_t_raw_u16 =
            canonical_texgen_decode_word(source, &offset);
        record->bias_s = canonical_texgen_decode_word(source, &offset);
        record->bias_t = canonical_texgen_decode_word(source, &offset);
        record->cylinder_s = canonical_texgen_decode_word(source, &offset);
        record->cylinder_t = canonical_texgen_decode_word(source, &offset);
    }

    if (offset != ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE ||
        !acgc_gx_canonical_texgen_state_validate(&decoded)) {
        return 0;
    }
    *destination = decoded;
    return 1;
}
