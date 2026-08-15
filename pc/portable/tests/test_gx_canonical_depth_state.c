#include "acgc/gx_canonical_depth_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

#if defined(__cplusplus)
#define DEPTH_TEST_ALIGNOF(type) alignof(type)
#else
#define DEPTH_TEST_ALIGNOF(type) _Alignof(type)
#endif

static void fill_depth_state(
    AcgcGxCanonicalDepthState* state,
    uint32_t z_compare_enable,
    uint32_t z_compare_func,
    uint32_t z_update_enable
) {
    memset(state, 0, sizeof(*state));
    state->z_compare_enable = z_compare_enable;
    state->z_compare_func = z_compare_func;
    state->z_update_enable = z_update_enable;
}

static AcgcGxCanonicalEnvelopeDirectoryEntry* depth_entry(
    AcgcGxCanonicalEnvelope* envelope
) {
    return &envelope->directory[
        ACGC_GX_CANONICAL_DEPTH_SECTION_ID - 1];
}

static void prepare_depth_envelope(AcgcGxCanonicalEnvelope* envelope) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_DEPTH_SECTION_MASK;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_DEPTH_SECTION_MASK;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_DEPTH_SECTION_BYTE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_DEPTH_SECTION_BYTE_SIZE;

    entry = depth_entry(envelope);
    entry->section_version = ACGC_GX_CANONICAL_DEPTH_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_DEPTH_SECTION_BYTE_SIZE;
    entry->count = ACGC_GX_CANONICAL_DEPTH_SECTION_COUNT;
    entry->capacity = ACGC_GX_CANONICAL_DEPTH_SECTION_CAPACITY;
    entry->valid_mask = ACGC_GX_CANONICAL_DEPTH_SECTION_MASK;
}

static int accepts_layout_and_boundaries(void) {
    uint32_t z_compare_enable;
    uint32_t z_compare_func;
    uint32_t z_update_enable;

    CHECK(sizeof(AcgcGxCanonicalDepthState) ==
          ACGC_GX_CANONICAL_DEPTH_STATE_SIZE);
    CHECK(DEPTH_TEST_ALIGNOF(AcgcGxCanonicalDepthState) ==
          ACGC_GX_CANONICAL_DEPTH_STATE_ALIGNMENT);
    CHECK(offsetof(AcgcGxCanonicalDepthState, z_compare_enable) == 0);
    CHECK(offsetof(AcgcGxCanonicalDepthState, z_compare_func) == 4);
    CHECK(offsetof(AcgcGxCanonicalDepthState, z_update_enable) == 8);
    CHECK(offsetof(AcgcGxCanonicalDepthState, reserved) == 12);

    for (z_compare_enable = ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MIN;
         z_compare_enable <= ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MAX;
         z_compare_enable++) {
        for (z_compare_func = ACGC_GX_CANONICAL_DEPTH_COMPARE_MIN;
             z_compare_func <= ACGC_GX_CANONICAL_DEPTH_COMPARE_MAX;
             z_compare_func++) {
            for (z_update_enable = ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MIN;
                 z_update_enable <= ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MAX;
                 z_update_enable++) {
                AcgcGxCanonicalDepthState state;

                fill_depth_state(
                    &state,
                    z_compare_enable,
                    z_compare_func,
                    z_update_enable
                );
                CHECK(acgc_gx_canonical_depth_state_validate(&state));
            }
        }
    }

    /* A disabled compare still retains the exact GXCompare value. */
    {
        AcgcGxCanonicalDepthState state;

        fill_depth_state(&state, 0, ACGC_GX_CANONICAL_DEPTH_COMPARE_MAX, 0);
        CHECK(acgc_gx_canonical_depth_state_validate(&state));
        CHECK(state.z_compare_enable == 0);
        CHECK(state.z_compare_func == ACGC_GX_CANONICAL_DEPTH_COMPARE_MAX);
        CHECK(state.z_update_enable == 0);
    }
    return 1;
}

static int rejects_null_unknown_and_reserved_words(void) {
    AcgcGxCanonicalDepthState state;

    CHECK(!acgc_gx_canonical_depth_state_validate(NULL));

    fill_depth_state(&state, 0, 0, 0);
    state.z_compare_enable =
        ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MAX + UINT32_C(1);
    CHECK(!acgc_gx_canonical_depth_state_validate(&state));

    fill_depth_state(&state, 0, 0, 0);
    state.z_compare_enable = UINT32_MAX;
    CHECK(!acgc_gx_canonical_depth_state_validate(&state));

    fill_depth_state(&state, 0, 0, 0);
    state.z_compare_func =
        ACGC_GX_CANONICAL_DEPTH_COMPARE_MAX + UINT32_C(1);
    CHECK(!acgc_gx_canonical_depth_state_validate(&state));

    fill_depth_state(&state, 0, 0, 0);
    state.z_compare_func = UINT32_MAX;
    CHECK(!acgc_gx_canonical_depth_state_validate(&state));

    fill_depth_state(&state, 0, 0, 0);
    state.z_update_enable =
        ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MAX + UINT32_C(1);
    CHECK(!acgc_gx_canonical_depth_state_validate(&state));

    fill_depth_state(&state, 0, 0, 0);
    state.z_update_enable = UINT32_MAX;
    CHECK(!acgc_gx_canonical_depth_state_validate(&state));

    fill_depth_state(&state, 0, 0, 0);
    state.reserved = 1;
    CHECK(!acgc_gx_canonical_depth_state_validate(&state));
    return 1;
}

static int accepts_exact_present_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_depth_envelope(&envelope);
    entry = depth_entry(&envelope);
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(acgc_gx_canonical_depth_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(entry->section_id == ACGC_GX_CANONICAL_DEPTH_SECTION_ID);
    CHECK(entry->section_version == 1);
    CHECK(entry->byte_size == 16);
    CHECK(entry->count == 1);
    CHECK(entry->capacity == 1);
    CHECK(entry->valid_mask == UINT32_C(0x0200));
    CHECK(entry->reserved == 0);
    return 1;
}

static int accepts_zero_absent_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = depth_entry(&envelope);
    CHECK((envelope.header.present_state_mask &
           ACGC_GX_CANONICAL_DEPTH_SECTION_MASK) == 0);
    CHECK((envelope.header.required_state_mask &
           ACGC_GX_CANONICAL_DEPTH_SECTION_MASK) == 0);
    CHECK(entry->section_id == ACGC_GX_CANONICAL_DEPTH_SECTION_ID);
    CHECK(entry->section_version == 0);
    CHECK(entry->byte_offset == 0);
    CHECK(entry->byte_size == 0);
    CHECK(entry->count == 0);
    CHECK(entry->capacity == 0);
    CHECK(entry->valid_mask == 0);
    CHECK(entry->reserved == 0);
    CHECK(acgc_gx_canonical_depth_metadata_validate(
        &envelope, sizeof(envelope)));
    return 1;
}

static int rejects_non_exact_present_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_depth_envelope(&envelope);
    entry = depth_entry(&envelope);
    entry->section_version = 2;
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_depth_envelope(&envelope);
    entry = depth_entry(&envelope);
    entry->valid_mask = ACGC_GX_CANONICAL_SECTION_MASK_BLEND;
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_depth_envelope(&envelope);
    entry = depth_entry(&envelope);
    entry->byte_size = 12;
    envelope.header.payload_byte_size = 12;
    envelope.header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 12;
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_depth_envelope(&envelope);
    entry = depth_entry(&envelope);
    entry->count = 2;
    entry->capacity = 2;
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_depth_envelope(&envelope);
    entry = depth_entry(&envelope);
    entry->capacity = 2;
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_depth_envelope(&envelope);
    entry = depth_entry(&envelope);
    entry->reserved = 1;
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    /* A present mask without a complete entry is a partial section. */
    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    envelope.header.present_state_mask =
        ACGC_GX_CANONICAL_DEPTH_SECTION_MASK;
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, sizeof(envelope)));
    return 1;
}

static int rejects_nonzero_absent_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = depth_entry(&envelope);
    entry->section_version = 1;
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, sizeof(envelope)));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = depth_entry(&envelope);
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, sizeof(envelope)));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = depth_entry(&envelope);
    entry->byte_size = ACGC_GX_CANONICAL_DEPTH_SECTION_BYTE_SIZE;
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, sizeof(envelope)));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = depth_entry(&envelope);
    entry->count = 1;
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, sizeof(envelope)));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = depth_entry(&envelope);
    entry->capacity = 1;
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, sizeof(envelope)));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = depth_entry(&envelope);
    entry->valid_mask = ACGC_GX_CANONICAL_DEPTH_SECTION_MASK;
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, sizeof(envelope)));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = depth_entry(&envelope);
    entry->reserved = 1;
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, sizeof(envelope)));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    envelope.header.required_state_mask =
        ACGC_GX_CANONICAL_DEPTH_SECTION_MASK;
    CHECK(!acgc_gx_canonical_depth_metadata_validate(
        &envelope, sizeof(envelope)));
    return 1;
}

int main(void) {
    if (!accepts_layout_and_boundaries() ||
        !rejects_null_unknown_and_reserved_words() ||
        !accepts_exact_present_metadata() ||
        !accepts_zero_absent_metadata() ||
        !rejects_non_exact_present_metadata() ||
        !rejects_nonzero_absent_metadata()) {
        return 1;
    }
    printf("GX canonical Depth tests: PASS\n");
    return 0;
}

#undef DEPTH_TEST_ALIGNOF
