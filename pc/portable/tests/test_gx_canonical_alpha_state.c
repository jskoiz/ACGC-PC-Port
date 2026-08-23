#include "acgc/gx_canonical_alpha_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static void fill_alpha_state(
    AcgcGxCanonicalAlphaState* state,
    uint32_t comp0,
    uint32_t ref0,
    uint32_t op,
    uint32_t comp1,
    uint32_t ref1,
    uint32_t color_update_enable,
    uint32_t alpha_update_enable,
    uint32_t z_comp_loc_before_tex
) {
    memset(state, 0, sizeof(*state));
    state->comp0 = comp0;
    state->ref0 = ref0;
    state->op = op;
    state->comp1 = comp1;
    state->ref1 = ref1;
    state->color_update_enable = color_update_enable;
    state->alpha_update_enable = alpha_update_enable;
    state->z_comp_loc_before_tex = z_comp_loc_before_tex;
}

static uint32_t read_le32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static AcgcGxCanonicalEnvelopeDirectoryEntry* alpha_entry(
    AcgcGxCanonicalEnvelope* envelope
) {
    return &envelope->directory[
        ACGC_GX_CANONICAL_ALPHA_SECTION_ID - 1];
}

static void prepare_alpha_envelope(AcgcGxCanonicalEnvelope* envelope) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_ALPHA_SECTION_MASK;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_ALPHA_SECTION_MASK;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_ALPHA_SECTION_BYTE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_ALPHA_SECTION_BYTE_SIZE;

    entry = alpha_entry(envelope);
    entry->section_version = ACGC_GX_CANONICAL_ALPHA_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_ALPHA_SECTION_BYTE_SIZE;
    entry->count = ACGC_GX_CANONICAL_ALPHA_SECTION_COUNT;
    entry->capacity = ACGC_GX_CANONICAL_ALPHA_SECTION_CAPACITY;
    entry->valid_mask = ACGC_GX_CANONICAL_ALPHA_SECTION_MASK;
}

static int accepts_boundaries_and_preserves_inactive_references(void) {
    AcgcGxCanonicalAlphaState state;

    fill_alpha_state(
        &state,
        ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN,
        ACGC_GX_CANONICAL_ALPHA_REFERENCE_MAX,
        ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN,
        ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX,
        ACGC_GX_CANONICAL_ALPHA_REFERENCE_MIN,
        ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MIN,
        ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX,
        ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX
    );
    CHECK(acgc_gx_canonical_alpha_state_validate(&state));
    CHECK(state.ref0 == ACGC_GX_CANONICAL_ALPHA_REFERENCE_MAX);
    CHECK(state.ref1 == ACGC_GX_CANONICAL_ALPHA_REFERENCE_MIN);

    /* ALWAYS/NEVER comparisons do not erase or normalize their references. */
    fill_alpha_state(&state, 7, 255, 3, 0, 254, 1, 0, 1);
    CHECK(acgc_gx_canonical_alpha_state_validate(&state));
    CHECK(state.comp0 == 7);
    CHECK(state.ref0 == 255);
    CHECK(state.op == 3);
    CHECK(state.comp1 == 0);
    CHECK(state.ref1 == 254);
    return 1;
}

static int accepts_independent_update_combinations(void) {
    uint32_t color_update_enable;
    uint32_t alpha_update_enable;
    uint32_t z_comp_loc_before_tex;

    for (color_update_enable = 0; color_update_enable <= 1;
         color_update_enable++) {
        for (alpha_update_enable = 0; alpha_update_enable <= 1;
             alpha_update_enable++) {
            for (z_comp_loc_before_tex = 0; z_comp_loc_before_tex <= 1;
                 z_comp_loc_before_tex++) {
                AcgcGxCanonicalAlphaState state;

                fill_alpha_state(
                    &state,
                    7,
                    255,
                    3,
                    0,
                    254,
                    color_update_enable,
                    alpha_update_enable,
                    z_comp_loc_before_tex
                );
                CHECK(acgc_gx_canonical_alpha_state_validate(&state));
            }
        }
    }
    return 1;
}

static int rejects_null_unknown_and_sentinel_words(void) {
    AcgcGxCanonicalAlphaState state;

    CHECK(!acgc_gx_canonical_alpha_state_validate(NULL));

    fill_alpha_state(&state, 0, 0, 0, 0, 0, 0, 0, 0);
    state.comp0 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX + 1;
    CHECK(!acgc_gx_canonical_alpha_state_validate(&state));

    fill_alpha_state(&state, 0, 0, 0, 0, 0, 0, 0, 0);
    state.ref0 = ACGC_GX_CANONICAL_ALPHA_REFERENCE_MAX + 1;
    CHECK(!acgc_gx_canonical_alpha_state_validate(&state));

    fill_alpha_state(&state, 0, 0, 0, 0, 0, 0, 0, 0);
    state.op = ACGC_GX_CANONICAL_ALPHA_OPERATOR_MAX + 1;
    CHECK(!acgc_gx_canonical_alpha_state_validate(&state));

    fill_alpha_state(&state, 0, 0, 0, 0, 0, 0, 0, 0);
    state.comp1 = UINT32_MAX;
    CHECK(!acgc_gx_canonical_alpha_state_validate(&state));

    fill_alpha_state(&state, 0, 0, 0, 0, 0, 0, 0, 0);
    state.ref1 = UINT32_MAX;
    CHECK(!acgc_gx_canonical_alpha_state_validate(&state));

    fill_alpha_state(&state, 0, 0, 0, 0, 0, 0, 0, 0);
    state.color_update_enable = 2;
    CHECK(!acgc_gx_canonical_alpha_state_validate(&state));

    fill_alpha_state(&state, 0, 0, 0, 0, 0, 0, 0, 0);
    state.alpha_update_enable = UINT32_MAX;
    CHECK(!acgc_gx_canonical_alpha_state_validate(&state));

    fill_alpha_state(&state, 0, 0, 0, 0, 0, 0, 0, 0);
    state.z_comp_loc_before_tex = 2;
    CHECK(!acgc_gx_canonical_alpha_state_validate(&state));
    return 1;
}

static int encodes_exact_alpha_words_and_preserves_output(void) {
    AcgcGxCanonicalAlphaState state;
    uint8_t bytes[ACGC_GX_CANONICAL_ALPHA_STATE_SIZE];
    uint8_t before[sizeof(bytes)];
    uint8_t short_bytes[ACGC_GX_CANONICAL_ALPHA_STATE_SIZE - 1];
    uint8_t short_before[sizeof(short_bytes)];

    CHECK(sizeof(bytes) == ACGC_GX_CANONICAL_ALPHA_STATE_SIZE);
    fill_alpha_state(&state, 1, 0xAB, 2, 3, 0xCD, 1, 0, 1);
    CHECK(acgc_gx_canonical_alpha_state_encode(
        &state, bytes, sizeof(bytes)));
    CHECK(read_le32(bytes + 0) == 1);
    CHECK(read_le32(bytes + 4) == 0xAB);
    CHECK(read_le32(bytes + 8) == 2);
    CHECK(read_le32(bytes + 12) == 3);
    CHECK(read_le32(bytes + 16) == 0xCD);
    CHECK(read_le32(bytes + 20) == 1);
    CHECK(read_le32(bytes + 24) == 0);
    CHECK(read_le32(bytes + 28) == 1);
    CHECK(bytes[4] == 0xAB && bytes[5] == 0 &&
          bytes[6] == 0 && bytes[7] == 0);

    memset(bytes, 0xA5, sizeof(bytes));
    memcpy(before, bytes, sizeof(bytes));
    state.comp1 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX + 1;
    CHECK(!acgc_gx_canonical_alpha_state_encode(
        &state, bytes, sizeof(bytes)));
    CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);

    fill_alpha_state(&state, 1, 0xAB, 2, 3, 0xCD, 1, 0, 1);
    memset(short_bytes, 0x5A, sizeof(short_bytes));
    memcpy(short_before, short_bytes, sizeof(short_bytes));
    CHECK(!acgc_gx_canonical_alpha_state_encode(
        &state, short_bytes, sizeof(short_bytes)));
    CHECK(memcmp(short_bytes, short_before, sizeof(short_bytes)) == 0);
    CHECK(!acgc_gx_canonical_alpha_state_encode(
        NULL, bytes, sizeof(bytes)));
    CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);
    CHECK(!acgc_gx_canonical_alpha_state_encode(
        &state, NULL, sizeof(bytes)));
    return 1;
}

static int accepts_exact_alpha_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_alpha_envelope(&envelope);
    entry = alpha_entry(&envelope);
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(acgc_gx_canonical_alpha_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(entry->section_id == ACGC_GX_CANONICAL_ALPHA_SECTION_ID);
    CHECK(entry->section_version == 1);
    CHECK(entry->byte_size == 32);
    CHECK(entry->count == 1);
    CHECK(entry->capacity == 1);
    CHECK(entry->valid_mask == UINT32_C(0x0100));
    CHECK(entry->reserved == 0);
    return 1;
}

static int accepts_zero_absent_alpha_entry(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = alpha_entry(&envelope);
    CHECK((envelope.header.present_state_mask &
           ACGC_GX_CANONICAL_ALPHA_SECTION_MASK) == 0);
    CHECK((envelope.header.required_state_mask &
           ACGC_GX_CANONICAL_ALPHA_SECTION_MASK) == 0);
    CHECK(entry->section_id == ACGC_GX_CANONICAL_ALPHA_SECTION_ID);
    CHECK(entry->section_version == 0);
    CHECK(entry->byte_offset == 0);
    CHECK(entry->byte_size == 0);
    CHECK(entry->count == 0);
    CHECK(entry->capacity == 0);
    CHECK(entry->valid_mask == 0);
    CHECK(entry->reserved == 0);
    CHECK(acgc_gx_canonical_alpha_metadata_validate(
        &envelope, sizeof(envelope)));
    return 1;
}

static int rejects_non_exact_present_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_alpha_envelope(&envelope);
    entry = alpha_entry(&envelope);
    entry->section_version = 2;
    CHECK(!acgc_gx_canonical_alpha_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_alpha_envelope(&envelope);
    entry = alpha_entry(&envelope);
    entry->valid_mask = UINT32_C(0x0080);
    CHECK(!acgc_gx_canonical_alpha_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_alpha_envelope(&envelope);
    entry = alpha_entry(&envelope);
    entry->byte_size = 28;
    envelope.header.payload_byte_size = 28;
    envelope.header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 28;
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(!acgc_gx_canonical_alpha_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_alpha_envelope(&envelope);
    entry = alpha_entry(&envelope);
    entry->count = 2;
    entry->capacity = 2;
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(!acgc_gx_canonical_alpha_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_alpha_envelope(&envelope);
    entry = alpha_entry(&envelope);
    entry->capacity = 2;
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(!acgc_gx_canonical_alpha_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    return 1;
}

static int rejects_nonzero_absent_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = alpha_entry(&envelope);
    entry->reserved = 1;
    CHECK(!acgc_gx_canonical_alpha_metadata_validate(
        &envelope, sizeof(envelope)));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = alpha_entry(&envelope);
    entry->byte_size = ACGC_GX_CANONICAL_ALPHA_SECTION_BYTE_SIZE;
    CHECK(!acgc_gx_canonical_alpha_metadata_validate(
        &envelope, sizeof(envelope)));
    return 1;
}

int main(void) {
    if (!accepts_boundaries_and_preserves_inactive_references() ||
        !accepts_independent_update_combinations() ||
        !rejects_null_unknown_and_sentinel_words() ||
        !encodes_exact_alpha_words_and_preserves_output() ||
        !accepts_exact_alpha_metadata() ||
        !accepts_zero_absent_alpha_entry() ||
        !rejects_non_exact_present_metadata() ||
        !rejects_nonzero_absent_metadata()) {
        return 1;
    }
    printf("GX canonical Alpha test/update tests: PASS\n");
    return 0;
}
