#include "acgc/gx_canonical_texgen_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", \
                    __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static const uint32_t k_finite_words[] = {
    UINT32_C(0x00000000), UINT32_C(0x80000000), UINT32_C(0x3F800000),
    UINT32_C(0xBF800000), UINT32_C(0x40000000), UINT32_C(0xC0000000),
    UINT32_C(0x40400000), UINT32_C(0xC0400000), UINT32_C(0x41000000),
    UINT32_C(0xC1000000), UINT32_C(0x7F7FFFFF), UINT32_C(0xFF7FFFFF)
};

static uint32_t finite_word(uint32_t index) {
    return k_finite_words[index %
        (sizeof(k_finite_words) / sizeof(k_finite_words[0]))];
}

static uint32_t popcount32(uint32_t value) {
    uint32_t count = 0;

    while (value != 0) {
        count += value & 1u;
        value >>= 1;
    }
    return count;
}

static void refresh_header_summary(AcgcGxCanonicalTexgenState* state) {
    uint32_t index;
    int all_texgen = 1;
    int all_ordinary =
        state->header.ordinary_matrix_known_mask ==
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_KNOWN_MASK;
    int all_post =
        state->header.post_matrix_known_mask ==
        ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_KNOWN_MASK;
    int all_su = 1;

    state->header.known_texgen_count =
        popcount32(state->header.texgen_known_mask);
    state->header.ordinary_matrix_count =
        popcount32(state->header.ordinary_matrix_known_mask);
    state->header.post_matrix_count =
        popcount32(state->header.post_matrix_known_mask);
    state->header.su_count = popcount32(state->header.su_known_mask);

    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_COUNT; index++) {
        if (state->texgen[index].component_known !=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL) {
            all_texgen = 0;
        }
        if (state->su[index].component_known !=
            ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_ALL) {
            all_su = 0;
        }
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT;
         index++) {
        if (state->ordinary_matrix[index].known_word_mask !=
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4) {
            all_ordinary = 0;
        }
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT;
         index++) {
        if (state->post_matrix[index].known_word_mask !=
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4) {
            all_post = 0;
        }
    }

    state->header.component_known_summary = 0;
    if (all_texgen) {
        state->header.component_known_summary |=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_TEXGEN;
    }
    if (all_ordinary) {
        state->header.component_known_summary |=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_ORDINARY_MATRIX;
    }
    if (all_post) {
        state->header.component_known_summary |=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_POST_MATRIX;
    }
    if (all_su) {
        state->header.component_known_summary |=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_SU;
    }
}

static void fill_state(AcgcGxCanonicalTexgenState* state) {
    uint32_t index;
    uint32_t word;

    memset(state, 0, sizeof(*state));
    state->header.active_texgen_count = 8;
    state->header.texgen_capacity = 8;
    state->header.ordinary_matrix_capacity = 11;
    state->header.post_matrix_capacity = 21;
    state->header.su_capacity = 8;
    state->header.texgen_known_mask = 0xFF;
    state->header.ordinary_matrix_known_mask = 0x7FF;
    state->header.post_matrix_known_mask = 0x1FFFFF;
    state->header.su_known_mask = 0xFF;

    state->texgen[0].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX2X4;
    state->texgen[0].source = ACGC_GX_CANONICAL_TEXGEN_SOURCE_POS;
    state->texgen[1].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX3X4;
    state->texgen[1].source = ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX0;
    state->texgen[2].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX2X4;
    state->texgen[2].source = ACGC_GX_CANONICAL_TEXGEN_SOURCE_NRM;
    state->texgen[3].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX3X4;
    state->texgen[3].source = ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR0;
    state->texgen[4].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP0;
    state->texgen[4].source =
        ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD0;
    state->texgen[5].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP1;
    state->texgen[5].source =
        ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD1;
    state->texgen[6].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP2;
    state->texgen[6].source =
        ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD2;
    state->texgen[7].function = ACGC_GX_CANONICAL_TEXGEN_FUNCTION_SRTG;
    state->texgen[7].source = ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR0;
    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_COUNT; index++) {
        state->texgen[index].ordinary_matrix_id =
            ACGC_GX_CANONICAL_TEXGEN_ORDINARY_LOGICAL_ID(index % 3);
        state->texgen[index].normalize = index & 1u;
        state->texgen[index].post_matrix_id =
            ACGC_GX_CANONICAL_TEXGEN_POST_LOGICAL_ID(index % 3);
        state->texgen[index].component_known =
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL;
    }

    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT;
         index++) {
        AcgcGxCanonicalTexgenMatrixRecord* record =
            &state->ordinary_matrix[index];

        record->logical_id =
            ACGC_GX_CANONICAL_TEXGEN_ORDINARY_LOGICAL_ID(index);
        record->last_load_type = index == 0 ?
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX2X4 :
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4;
        record->last_written_word_count = index == 0 ? 8 : 12;
        record->known_word_mask =
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4;
        for (word = 0; word < 12; word++) {
            record->words[word] = finite_word(index + word);
        }
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT;
         index++) {
        AcgcGxCanonicalTexgenMatrixRecord* record =
            &state->post_matrix[index];

        record->logical_id =
            ACGC_GX_CANONICAL_TEXGEN_POST_LOGICAL_ID(index);
        record->last_load_type =
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4;
        record->last_written_word_count = 12;
        record->known_word_mask =
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4;
        for (word = 0; word < 12; word++) {
            record->words[word] = finite_word(index + word + 2);
        }
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_SU_COUNT; index++) {
        AcgcGxCanonicalTexgenSuRecord* record = &state->su[index];

        record->component_known =
            ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_ALL;
        record->manual_enable = index & 1u;
        record->scale_s_raw_u16 = index & 1u ? 0xFFFFu : 0x0100u;
        record->scale_t_raw_u16 = index & 1u ? 0x0000u : 0x0101u;
        record->bias_s = index & 1u;
        record->bias_t = (index + 1u) & 1u;
        record->cylinder_s = index & 1u;
        record->cylinder_t = (index + 1u) & 1u;
    }
    refresh_header_summary(state);
}

static void write_le32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)(value & 0xFFu);
    destination[1] = (uint8_t)((value >> 8) & 0xFFu);
    destination[2] = (uint8_t)((value >> 16) & 0xFFu);
    destination[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static void prepare_present_envelope(AcgcGxCanonicalEnvelope* envelope) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_TEXGEN_SECTION_MASK;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_TEXGEN_SECTION_MASK;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_TEXGEN_SECTION_BYTE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_TEXGEN_SECTION_BYTE_SIZE;

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_TEXGEN_SECTION_ID - 1];
    entry->section_version = ACGC_GX_CANONICAL_TEXGEN_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_TEXGEN_SECTION_BYTE_SIZE;
    entry->count = ACGC_GX_CANONICAL_TEXGEN_SECTION_COUNT;
    entry->capacity = ACGC_GX_CANONICAL_TEXGEN_SECTION_CAPACITY;
    entry->valid_mask = ACGC_GX_CANONICAL_TEXGEN_SECTION_MASK;
}

static int accepts_layout_and_metadata(void) {
    AcgcGxCanonicalTexgenState state;
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    CHECK(sizeof(AcgcGxCanonicalTexgenState) == 0xA40);
    CHECK(_Alignof(AcgcGxCanonicalTexgenState) == 4);
    CHECK(offsetof(AcgcGxCanonicalTexgenState, header) == 0x000);
    CHECK(offsetof(AcgcGxCanonicalTexgenState, texgen) == 0x040);
    CHECK(offsetof(AcgcGxCanonicalTexgenState, ordinary_matrix) == 0x140);
    CHECK(offsetof(AcgcGxCanonicalTexgenState, post_matrix) == 0x400);
    CHECK(offsetof(AcgcGxCanonicalTexgenState, su) == 0x940);
    CHECK(sizeof(AcgcGxCanonicalTexgenRecord) == 0x20);
    CHECK(sizeof(AcgcGxCanonicalTexgenMatrixRecord) == 0x40);
    CHECK(sizeof(AcgcGxCanonicalTexgenSuRecord) == 0x20);
    CHECK(ACGC_GX_CANONICAL_TEXGEN_SECTION_ID == 4);
    CHECK(ACGC_GX_CANONICAL_TEXGEN_SECTION_MASK == 0x0008);
    CHECK(ACGC_GX_CANONICAL_TEXGEN_SECTION_BYTE_SIZE == 0xA40);

    fill_state(&state);
    CHECK(acgc_gx_canonical_texgen_state_validate(&state));
    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    CHECK(acgc_gx_canonical_texgen_metadata_validate(
        &envelope, sizeof(envelope)));
    prepare_present_envelope(&envelope);
    CHECK(acgc_gx_canonical_texgen_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_TEXGEN_SECTION_ID - 1];
    CHECK(entry->section_id == 4);
    CHECK(entry->section_version == 1);
    CHECK(entry->byte_offset == ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET);
    CHECK(entry->byte_size == 0xA40);
    CHECK(entry->count == 1);
    CHECK(entry->capacity == 1);
    CHECK(entry->valid_mask == 0x0008);
    return 1;
}

static int round_trips_explicit_little_endian_words(void) {
    AcgcGxCanonicalTexgenState state;
    AcgcGxCanonicalTexgenState decoded;
    uint8_t wire[ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE];
    uint8_t roundtrip[ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE];

    fill_state(&state);
    CHECK(acgc_gx_canonical_texgen_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(read_le32(wire + 0x000) == 8);
    CHECK(read_le32(wire + 0x004) == 8);
    CHECK(read_le32(wire + 0x008) == 8);
    CHECK(read_le32(wire + 0x00C) == 11);
    CHECK(read_le32(wire + 0x010) == 11);
    CHECK(read_le32(wire + 0x014) == 21);
    CHECK(read_le32(wire + 0x018) == 21);
    CHECK(read_le32(wire + 0x01C) == 8);
    CHECK(read_le32(wire + 0x024) == 0xFF);
    CHECK(read_le32(wire + 0x028) == 0x7FF);
    CHECK(read_le32(wire + 0x02C) == 0x1FFFFF);
    CHECK(read_le32(wire + 0x030) == 0xFF);
    CHECK(read_le32(wire + 0x034) == 0x0F);
    CHECK(read_le32(wire + 0x040) ==
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX2X4);
    CHECK(read_le32(wire + 0x140) == 30);
    CHECK(read_le32(wire + 0x400) == 64);
    CHECK(read_le32(wire + 0x940) ==
        ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_ALL);
    CHECK(wire[0x944] == 1 || wire[0x944] == 0);

    memset(&decoded, 0xA5, sizeof(decoded));
    CHECK(acgc_gx_canonical_texgen_state_decode(
        wire, sizeof(wire), &decoded));
    CHECK(memcmp(&state, &decoded, sizeof(state)) == 0);
    CHECK(acgc_gx_canonical_texgen_state_encode(
        &decoded, roundtrip, sizeof(roundtrip)));
    CHECK(memcmp(wire, roundtrip, sizeof(wire)) == 0);
    return 1;
}

static int preserves_outputs_on_failure(void) {
    AcgcGxCanonicalTexgenState state;
    AcgcGxCanonicalTexgenState before;
    uint8_t wire[ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE];
    uint8_t bad_wire[ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE];
    uint8_t output[ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE];
    uint8_t output_before[ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE];

    fill_state(&state);
    CHECK(!acgc_gx_canonical_texgen_state_encode(
        &state, NULL, sizeof(wire)));
    CHECK(!acgc_gx_canonical_texgen_state_encode(
        &state, output, sizeof(output) - 1));
    memset(output, 0xA5, sizeof(output));
    memcpy(output_before, output, sizeof(output));
    state.header.reserved[0] = 1;
    CHECK(!acgc_gx_canonical_texgen_state_encode(
        &state, output, sizeof(output)));
    CHECK(memcmp(output, output_before, sizeof(output)) == 0);

    fill_state(&state);
    CHECK(acgc_gx_canonical_texgen_state_encode(
        &state, wire, sizeof(wire)));
    memcpy(bad_wire, wire, sizeof(bad_wire));
    write_le32(bad_wire + 0x004, 7);
    fill_state(&before);
    state = before;
    CHECK(!acgc_gx_canonical_texgen_state_decode(
        bad_wire, sizeof(bad_wire), &state));
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
    CHECK(!acgc_gx_canonical_texgen_state_decode(
        bad_wire, sizeof(bad_wire) - 1, &state));
    CHECK(!acgc_gx_canonical_texgen_state_decode(
        bad_wire, sizeof(bad_wire) + 1, &state));
    return 1;
}

static int rejects_selector_and_order_errors(void) {
    AcgcGxCanonicalTexgenState state;

    fill_state(&state);
    state.texgen[0].function = 11;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.texgen[0].source =
        ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD0;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.texgen[0].ordinary_matrix_id = 31;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.texgen[0].post_matrix_id = 66;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.texgen[0].normalize = 2;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.texgen[4].function = ACGC_GX_CANONICAL_TEXGEN_FUNCTION_SRTG;
    state.texgen[4].source = ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR0;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.texgen[4].source =
        ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD6;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.header.active_texgen_count = 1;
    state.texgen[0].function = ACGC_GX_CANONICAL_TEXGEN_FUNCTION_SRTG;
    state.texgen[0].source = ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR1;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.header.active_texgen_count = 2;
    state.texgen[0].function = ACGC_GX_CANONICAL_TEXGEN_FUNCTION_SRTG;
    state.texgen[0].source = ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR0;
    state.texgen[1].function = ACGC_GX_CANONICAL_TEXGEN_FUNCTION_SRTG;
    state.texgen[1].source = ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR0;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.texgen[7].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP0;
    state.texgen[7].source =
        ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD0;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.header.active_texgen_count = 1;
    state.texgen[0].component_known = 0;
    state.texgen[0].function = 0;
    state.texgen[0].source = 0;
    state.texgen[0].ordinary_matrix_id = 0;
    state.texgen[0].normalize = 0;
    state.texgen[0].post_matrix_id = 0;
    state.header.texgen_known_mask &= ~UINT32_C(1);
    refresh_header_summary(&state);
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));
    return 1;
}

static int accepts_inactive_unknown_and_rejects_nonzero_unknowns(void) {
    AcgcGxCanonicalTexgenState state;

    fill_state(&state);
    state.header.active_texgen_count = 0;
    state.texgen[0].component_known = 0;
    state.texgen[0].function = 0;
    state.texgen[0].source = 0;
    state.texgen[0].ordinary_matrix_id = 0;
    state.texgen[0].normalize = 0;
    state.texgen[0].post_matrix_id = 0;
    state.header.texgen_known_mask &= ~UINT32_C(1);
    refresh_header_summary(&state);
    CHECK(acgc_gx_canonical_texgen_state_validate(&state));

    state.texgen[0].source = 1;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));
    return 1;
}

static int accepts_matrix_range_preservation_and_rejects_bad_matrices(void) {
    AcgcGxCanonicalTexgenState state;

    fill_state(&state);
    state.ordinary_matrix[0].last_load_type =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX2X4;
    state.ordinary_matrix[0].last_written_word_count = 8;
    state.ordinary_matrix[0].known_word_mask =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_2X4;
    memset(&state.ordinary_matrix[0].words[8], 0, 4 * sizeof(uint32_t));
    state.header.active_texgen_count = 1;
    state.header.component_known_summary &=
        ~ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_ORDINARY_MATRIX;
    CHECK(acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.ordinary_matrix[0].last_load_type =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4;
    state.ordinary_matrix[0].last_written_word_count = 8;
    state.ordinary_matrix[0].known_word_mask =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_2X4;
    memset(&state.ordinary_matrix[0].words[8], 0, 4 * sizeof(uint32_t));
    state.header.component_known_summary &=
        ~ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_ORDINARY_MATRIX;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.post_matrix[0].last_load_type =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX2X4;
    state.post_matrix[0].last_written_word_count = 8;
    state.post_matrix[0].known_word_mask =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_2X4;
    memset(&state.post_matrix[0].words[8], 0, 4 * sizeof(uint32_t));
    state.header.component_known_summary &=
        ~ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_POST_MATRIX;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.ordinary_matrix[0].words[0] = UINT32_C(0x7F800000);
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.header.active_texgen_count = 0;
    state.header.ordinary_matrix_known_mask &= ~UINT32_C(1);
    state.ordinary_matrix[0].last_load_type = 0;
    state.ordinary_matrix[0].last_written_word_count = 0;
    state.ordinary_matrix[0].known_word_mask = 0;
    memset(state.ordinary_matrix[0].words, 0,
           sizeof(state.ordinary_matrix[0].words));
    refresh_header_summary(&state);
    CHECK(acgc_gx_canonical_texgen_state_validate(&state));
    state.ordinary_matrix[0].words[0] = UINT32_C(0x3F800000);
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.ordinary_matrix[10].logical_id = 59;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.post_matrix[20].logical_id = 124;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.ordinary_matrix[0].known_word_mask = UINT32_C(0x1000);
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));
    return 1;
}

static int accepts_raw_su_semantics_and_rejects_bad_values(void) {
    AcgcGxCanonicalTexgenState state;

    fill_state(&state);
    state.su[0].manual_enable = 0;
    state.su[0].scale_s_raw_u16 = 0xFFFF;
    state.su[0].scale_t_raw_u16 = 0xFFFF;
    CHECK(acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.su[0].component_known =
        ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_MANUAL;
    state.su[0].manual_enable = 0;
    state.su[0].scale_s_raw_u16 = 0;
    state.su[0].scale_t_raw_u16 = 0;
    state.su[0].bias_s = 0;
    state.su[0].bias_t = 0;
    state.su[0].cylinder_s = 0;
    state.su[0].cylinder_t = 0;
    state.header.su_known_mask = 0xFF;
    refresh_header_summary(&state);
    CHECK(acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.su[0].component_known =
        ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_MANUAL;
    state.su[0].manual_enable = 2;
    state.su[0].scale_s_raw_u16 = 0;
    state.su[0].scale_t_raw_u16 = 0;
    state.su[0].bias_s = 0;
    state.su[0].bias_t = 0;
    state.su[0].cylinder_s = 0;
    state.su[0].cylinder_t = 0;
    refresh_header_summary(&state);
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.su[0].component_known =
        ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_MANUAL;
    state.su[0].manual_enable = 0;
    state.su[0].scale_s_raw_u16 = UINT32_C(0x10000);
    state.su[0].scale_t_raw_u16 = 0;
    state.su[0].bias_s = 0;
    state.su[0].bias_t = 0;
    state.su[0].cylinder_s = 0;
    state.su[0].cylinder_t = 0;
    refresh_header_summary(&state);
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.su[0].component_known =
        ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_BIAS_S;
    state.su[0].manual_enable = 0;
    state.su[0].scale_s_raw_u16 = 0;
    state.su[0].scale_t_raw_u16 = 0;
    state.su[0].bias_s = 2;
    state.su[0].bias_t = 0;
    state.su[0].cylinder_s = 0;
    state.su[0].cylinder_t = 0;
    refresh_header_summary(&state);
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));
    return 1;
}

static int rejects_bad_header_and_metadata(void) {
    AcgcGxCanonicalTexgenState state;
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    fill_state(&state);
    state.header.texgen_capacity = 7;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.header.known_texgen_count = 7;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    fill_state(&state);
    state.header.reserved[1] = 1;
    CHECK(!acgc_gx_canonical_texgen_state_validate(&state));

    prepare_present_envelope(&envelope);
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_TEXGEN_SECTION_ID - 1];
    entry->section_version = 2;
    CHECK(!acgc_gx_canonical_texgen_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_present_envelope(&envelope);
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_TEXGEN_SECTION_ID - 1];
    entry->capacity = 2;
    CHECK(!acgc_gx_canonical_texgen_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_TEXGEN_SECTION_ID - 1];
    entry->section_version = 1;
    CHECK(!acgc_gx_canonical_texgen_metadata_validate(
        &envelope, sizeof(envelope)));
    return 1;
}

int main(void) {
    if (!accepts_layout_and_metadata() ||
        !round_trips_explicit_little_endian_words() ||
        !preserves_outputs_on_failure() ||
        !rejects_selector_and_order_errors() ||
        !accepts_inactive_unknown_and_rejects_nonzero_unknowns() ||
        !accepts_matrix_range_preservation_and_rejects_bad_matrices() ||
        !accepts_raw_su_semantics_and_rejects_bad_values() ||
        !rejects_bad_header_and_metadata()) {
        return 1;
    }
    puts("canonical Texgen/SU state tests passed");
    return 0;
}
