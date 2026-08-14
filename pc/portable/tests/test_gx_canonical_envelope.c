#include "acgc/gx_canonical_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static void fill_fog_state(AcgcGxCanonicalFogState* state) {
    uint32_t index;

    memset(state, 0, sizeof(*state));
    state->fog_type = ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN;
    state->start_bits = UINT32_C(0x3F800000);
    state->end_bits = UINT32_C(0x40000000);
    state->near_bits = UINT32_C(0x3F000000);
    state->far_bits = UINT32_C(0x40A00000);
    state->color_rgba8 = UINT32_C(0x44332211);
    state->range_adjust_enable = 1;
    state->range_center = 320;
    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        state->range_adjust[index] = UINT32_C(0x100) + index;
    }
}

static void prepare_fog_envelope(
    AcgcGxCanonicalEnvelope* envelope,
    AcgcGxCanonicalFogState* fog
) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    fill_fog_state(fog);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_SECTION_MASK_FOG;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_SECTION_MASK_FOG;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_FOG_STATE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_FOG_STATE_SIZE;

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_SECTION_ID_FOG - 1];
    entry->section_version = ACGC_GX_CANONICAL_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_FOG_STATE_SIZE;
    entry->count = 1;
    entry->capacity = 1;
    entry->valid_mask = ACGC_GX_CANONICAL_SECTION_MASK_FOG;
}

static int accepts_empty_metadata_prefix(void) {
    AcgcGxCanonicalEnvelope envelope;

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    CHECK(envelope.header.present_state_mask == 0);
    CHECK(envelope.header.payload_byte_size == 0);
    CHECK(envelope.header.total_byte_size ==
          ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET);
    CHECK(!acgc_gx_canonical_envelope_validate(NULL, 0));
    CHECK(!acgc_gx_canonical_envelope_validate(&envelope, sizeof(envelope) - 1));
    CHECK(acgc_gx_canonical_envelope_validate(&envelope, sizeof(envelope)));
    return 1;
}

static int accepts_fog_metadata_and_payload_contract(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalFogState fog;

    prepare_fog_envelope(&envelope, &fog);
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(acgc_gx_canonical_fog_state_validate(&fog));
    CHECK(sizeof(AcgcGxCanonicalFogState) ==
          ACGC_GX_CANONICAL_FOG_STATE_SIZE);
    return 1;
}

static int accepts_dynamic_known_section_extent(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalFogState fog;
    AcgcGxCanonicalEnvelopeDirectoryEntry* blend;

    prepare_fog_envelope(&envelope, &fog);
    envelope.header.present_state_mask |=
        ACGC_GX_CANONICAL_SECTION_MASK_BLEND;
    blend = &envelope.directory[
        ACGC_GX_CANONICAL_SECTION_ID_BLEND - 1];
    blend->section_version = ACGC_GX_CANONICAL_SECTION_VERSION;
    blend->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    blend->byte_size = 12;
    blend->count = 2;
    blend->capacity = 4;
    blend->valid_mask = ACGC_GX_CANONICAL_SECTION_MASK_BLEND;
    envelope.directory[ACGC_GX_CANONICAL_SECTION_ID_FOG - 1].byte_offset =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 12;
    envelope.header.payload_byte_size = 92;
    envelope.header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 92;

    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    return 1;
}

static int rejects_header_masks_versions_and_reserved_words(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalFogState fog;

    prepare_fog_envelope(&envelope, &fog);
    envelope.header.known_state_mask |= UINT32_C(0x4000);
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    envelope.header.present_state_mask |= UINT32_C(0x4000);
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    envelope.header.required_state_mask |= UINT32_C(0x4000);
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    envelope.header.version++;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    envelope.header.reserved = 1;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    envelope.directory[ACGC_GX_CANONICAL_SECTION_ID_FOG - 1].section_version++;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    return 1;
}

static int rejects_directory_identity_and_presence_mismatches(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalFogState fog;

    prepare_fog_envelope(&envelope, &fog);
    envelope.directory[0].section_id =
        ACGC_GX_CANONICAL_SECTION_ID_TRANSFORMS;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    envelope.directory[1].section_id =
        ACGC_GX_CANONICAL_SECTION_ID_GEOMETRY;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    envelope.directory[ACGC_GX_CANONICAL_SECTION_ID_CHANNELS - 1].reserved = 1;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    envelope.directory[ACGC_GX_CANONICAL_SECTION_ID_CHANNELS - 1].byte_size = 4;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    envelope.header.present_state_mask |=
        ACGC_GX_CANONICAL_SECTION_MASK_CHANNELS;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    envelope.directory[ACGC_GX_CANONICAL_SECTION_ID_FOG - 1].valid_mask = 0;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    return 1;
}

static int rejects_fog_size_count_and_capacity_mismatches(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalFogState fog;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_fog_envelope(&envelope, &fog);
    entry = &envelope.directory[ACGC_GX_CANONICAL_SECTION_ID_FOG - 1];
    entry->byte_size = 76;
    envelope.header.payload_byte_size = 76;
    envelope.header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 76;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    entry = &envelope.directory[ACGC_GX_CANONICAL_SECTION_ID_FOG - 1];
    entry->count = 0;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    entry = &envelope.directory[ACGC_GX_CANONICAL_SECTION_ID_FOG - 1];
    entry->count = 2;
    entry->capacity = 1;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    entry = &envelope.directory[ACGC_GX_CANONICAL_SECTION_ID_FOG - 1];
    entry->capacity = 0;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    entry = &envelope.directory[ACGC_GX_CANONICAL_SECTION_ID_FOG - 1];
    entry->byte_offset += 2;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    return 1;
}

static int rejects_layout_extent_and_nonzero_inactive_fields(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalFogState fog;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_fog_envelope(&envelope, &fog);
    envelope.header.payload_offset += 4;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    entry = &envelope.directory[ACGC_GX_CANONICAL_SECTION_ID_FOG - 1];
    entry->byte_offset += 4;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    envelope.header.payload_byte_size += 4;
    envelope.header.total_byte_size += 4;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    entry = &envelope.directory[ACGC_GX_CANONICAL_SECTION_ID_FOG - 1];
    entry->byte_size = 84;
    envelope.header.payload_byte_size = 84;
    envelope.header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 84;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_fog_envelope(&envelope, &fog);
    envelope.directory[ACGC_GX_CANONICAL_SECTION_ID_CHANNELS - 1].section_version = 1;
    CHECK(!acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    return 1;
}

int main(void) {
    if (!accepts_empty_metadata_prefix() ||
        !accepts_fog_metadata_and_payload_contract() ||
        !accepts_dynamic_known_section_extent() ||
        !rejects_header_masks_versions_and_reserved_words() ||
        !rejects_directory_identity_and_presence_mismatches() ||
        !rejects_fog_size_count_and_capacity_mismatches() ||
        !rejects_layout_extent_and_nonzero_inactive_fields()) {
        return 1;
    }
    printf("GX canonical envelope tests: PASS\n");
    return 0;
}
