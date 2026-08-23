#include "acgc/gx_canonical_blend_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static void fill_blend_state(
    AcgcGxCanonicalBlendState* state,
    uint32_t mode,
    uint32_t source_factor,
    uint32_t destination_factor,
    uint32_t logic_op
) {
    memset(state, 0, sizeof(*state));
    state->mode = mode;
    state->source_factor = source_factor;
    state->destination_factor = destination_factor;
    state->logic_op = logic_op;
}

static uint32_t read_le32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static AcgcGxCanonicalEnvelopeDirectoryEntry* blend_entry(
    AcgcGxCanonicalEnvelope* envelope
) {
    return &envelope->directory[
        ACGC_GX_CANONICAL_BLEND_SECTION_ID - 1];
}

static void prepare_blend_envelope(AcgcGxCanonicalEnvelope* envelope) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_BLEND_SECTION_MASK;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_BLEND_SECTION_MASK;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_BLEND_SECTION_BYTE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_BLEND_SECTION_BYTE_SIZE;

    entry = blend_entry(envelope);
    entry->section_version = ACGC_GX_CANONICAL_BLEND_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_BLEND_SECTION_BYTE_SIZE;
    entry->count = ACGC_GX_CANONICAL_BLEND_SECTION_COUNT;
    entry->capacity = ACGC_GX_CANONICAL_BLEND_SECTION_CAPACITY;
    entry->valid_mask = ACGC_GX_CANONICAL_BLEND_SECTION_MASK;
}

static int accepts_bounded_words_without_normalization(void) {
    AcgcGxCanonicalBlendState state;

    /* GX_BM_NONE still transports bounded factor and logic words. */
    fill_blend_state(
        &state,
        ACGC_GX_CANONICAL_BLEND_MODE_MIN,
        ACGC_GX_CANONICAL_BLEND_FACTOR_MAX,
        ACGC_GX_CANONICAL_BLEND_FACTOR_MIN,
        ACGC_GX_CANONICAL_BLEND_LOGIC_OP_MAX
    );
    CHECK(acgc_gx_canonical_blend_state_validate(&state));

    /* GX_BM_LOGIC still transports both factor slots and GX_LO_NOOP (5). */
    fill_blend_state(&state, 2, 2, 3, 5);
    CHECK(acgc_gx_canonical_blend_state_validate(&state));

    /* GX_BM_SUBTRACT still transports a bounded logic word. */
    fill_blend_state(
        &state,
        ACGC_GX_CANONICAL_BLEND_MODE_MAX,
        6,
        7,
        ACGC_GX_CANONICAL_BLEND_LOGIC_OP_MIN
    );
    CHECK(acgc_gx_canonical_blend_state_validate(&state));
    return 1;
}

static int rejects_null_unknown_and_sentinel_words(void) {
    AcgcGxCanonicalBlendState state;

    CHECK(!acgc_gx_canonical_blend_state_validate(NULL));

    fill_blend_state(&state, 0, 0, 0, 0);
    state.mode = ACGC_GX_CANONICAL_BLEND_MODE_MAX + 1;
    CHECK(!acgc_gx_canonical_blend_state_validate(&state));

    fill_blend_state(&state, 0, 0, 0, 0);
    state.source_factor = ACGC_GX_CANONICAL_BLEND_FACTOR_MAX + 1;
    CHECK(!acgc_gx_canonical_blend_state_validate(&state));

    fill_blend_state(&state, 0, 0, 0, 0);
    state.destination_factor = UINT32_MAX;
    CHECK(!acgc_gx_canonical_blend_state_validate(&state));

    fill_blend_state(&state, 0, 0, 0, 0);
    state.logic_op = ACGC_GX_CANONICAL_BLEND_LOGIC_OP_MAX + 1;
    CHECK(!acgc_gx_canonical_blend_state_validate(&state));
    return 1;
}

static int encodes_exact_blend_words_and_preserves_output(void) {
    AcgcGxCanonicalBlendState state;
    uint8_t bytes[ACGC_GX_CANONICAL_BLEND_STATE_SIZE];
    uint8_t before[sizeof(bytes)];
    uint8_t short_bytes[ACGC_GX_CANONICAL_BLEND_STATE_SIZE - 1];
    uint8_t short_before[sizeof(short_bytes)];

    CHECK(sizeof(bytes) == ACGC_GX_CANONICAL_BLEND_STATE_SIZE);
    fill_blend_state(&state, 3, 4, 5, 6);
    CHECK(acgc_gx_canonical_blend_state_encode(
        &state, bytes, sizeof(bytes)));
    CHECK(read_le32(bytes + 0) == 3);
    CHECK(read_le32(bytes + 4) == 4);
    CHECK(read_le32(bytes + 8) == 5);
    CHECK(read_le32(bytes + 12) == 6);
    CHECK(bytes[0] == 3 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0);

    memset(bytes, 0xA5, sizeof(bytes));
    memcpy(before, bytes, sizeof(bytes));
    state.mode = ACGC_GX_CANONICAL_BLEND_MODE_MAX + 1;
    CHECK(!acgc_gx_canonical_blend_state_encode(
        &state, bytes, sizeof(bytes)));
    CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);

    fill_blend_state(&state, 3, 4, 5, 6);
    memset(short_bytes, 0x5A, sizeof(short_bytes));
    memcpy(short_before, short_bytes, sizeof(short_bytes));
    CHECK(!acgc_gx_canonical_blend_state_encode(
        &state, short_bytes, sizeof(short_bytes)));
    CHECK(memcmp(short_bytes, short_before, sizeof(short_bytes)) == 0);
    CHECK(!acgc_gx_canonical_blend_state_encode(
        NULL, bytes, sizeof(bytes)));
    CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);
    CHECK(!acgc_gx_canonical_blend_state_encode(
        &state, NULL, sizeof(bytes)));
    return 1;
}

static int accepts_exact_blend_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_blend_envelope(&envelope);
    entry = blend_entry(&envelope);
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(acgc_gx_canonical_blend_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(entry->section_id == ACGC_GX_CANONICAL_BLEND_SECTION_ID);
    CHECK(entry->section_version == 1);
    CHECK(entry->byte_size == 16);
    CHECK(entry->count == 1);
    CHECK(entry->capacity == 1);
    CHECK(entry->valid_mask == UINT32_C(0x0080));
    CHECK(entry->reserved == 0);
    return 1;
}

static int accepts_zero_absent_blend_entry(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = blend_entry(&envelope);
    CHECK((envelope.header.present_state_mask &
           ACGC_GX_CANONICAL_BLEND_SECTION_MASK) == 0);
    CHECK((envelope.header.required_state_mask &
           ACGC_GX_CANONICAL_BLEND_SECTION_MASK) == 0);
    CHECK(entry->section_id == ACGC_GX_CANONICAL_BLEND_SECTION_ID);
    CHECK(entry->section_version == 0);
    CHECK(entry->byte_offset == 0);
    CHECK(entry->byte_size == 0);
    CHECK(entry->count == 0);
    CHECK(entry->capacity == 0);
    CHECK(entry->valid_mask == 0);
    CHECK(entry->reserved == 0);
    CHECK(acgc_gx_canonical_blend_metadata_validate(&envelope, sizeof(envelope)));
    return 1;
}

static int rejects_non_exact_present_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_blend_envelope(&envelope);
    entry = blend_entry(&envelope);
    entry->section_version = 2;
    CHECK(!acgc_gx_canonical_blend_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_blend_envelope(&envelope);
    entry = blend_entry(&envelope);
    entry->valid_mask = UINT32_C(0x0040);
    CHECK(!acgc_gx_canonical_blend_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_blend_envelope(&envelope);
    entry = blend_entry(&envelope);
    entry->byte_size = 12;
    envelope.header.payload_byte_size = 12;
    envelope.header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 12;
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(!acgc_gx_canonical_blend_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_blend_envelope(&envelope);
    entry = blend_entry(&envelope);
    entry->count = 2;
    entry->capacity = 2;
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(!acgc_gx_canonical_blend_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_blend_envelope(&envelope);
    entry = blend_entry(&envelope);
    entry->capacity = 2;
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(!acgc_gx_canonical_blend_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    return 1;
}

static int rejects_nonzero_absent_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = blend_entry(&envelope);
    entry->reserved = 1;
    CHECK(!acgc_gx_canonical_blend_metadata_validate(&envelope, sizeof(envelope)));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = blend_entry(&envelope);
    entry->byte_size = ACGC_GX_CANONICAL_BLEND_SECTION_BYTE_SIZE;
    CHECK(!acgc_gx_canonical_blend_metadata_validate(&envelope, sizeof(envelope)));
    return 1;
}

int main(void) {
    if (!accepts_bounded_words_without_normalization() ||
        !rejects_null_unknown_and_sentinel_words() ||
        !encodes_exact_blend_words_and_preserves_output() ||
        !accepts_exact_blend_metadata() ||
        !accepts_zero_absent_blend_entry() ||
        !rejects_non_exact_present_metadata() ||
        !rejects_nonzero_absent_metadata()) {
        return 1;
    }
    printf("GX canonical Blend/logic tests: PASS\n");
    return 0;
}
