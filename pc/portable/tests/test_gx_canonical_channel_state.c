#include "acgc/gx_canonical_channel_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static void fill_control(
    AcgcGxCanonicalChannelControl* control,
    uint32_t enable,
    uint32_t ambient_source,
    uint32_t material_source,
    uint32_t light_mask,
    uint32_t diffuse_function,
    uint32_t attenuation_function
) {
    control->enable = enable;
    control->ambient_source = ambient_source;
    control->material_source = material_source;
    control->light_mask = light_mask;
    control->diffuse_function = diffuse_function;
    control->attenuation_function = attenuation_function;
}

static void fill_valid_state(AcgcGxCanonicalChannelState* state) {
    memset(state, 0, sizeof(*state));
    state->active_count = 2;
    state->record_valid_mask = UINT32_C(0x3);

    state->records[0].channel_index = 0;
    fill_control(&state->records[0].color, 1, 0, 1, 0x01, 1, 1);
    fill_control(&state->records[0].alpha, 0, 1, 0, 0x02, 0, 2);
    state->records[0].ambient_rgba8 = UINT32_C(0x44332211);
    state->records[0].material_rgba8 = UINT32_C(0x88776655);

    state->records[1].channel_index = 1;
    fill_control(&state->records[1].color, 0, 1, 0, 0x80, 0, 0);
    fill_control(&state->records[1].alpha, 1, 0, 1, 0xFF, 2, 2);
    state->records[1].ambient_rgba8 = UINT32_C(0x01020304);
    state->records[1].material_rgba8 = UINT32_C(0xA0B0C0D0);
}

static AcgcGxCanonicalEnvelopeDirectoryEntry* channel_entry(
    AcgcGxCanonicalEnvelope* envelope
) {
    return &envelope->directory[
        ACGC_GX_CANONICAL_CHANNEL_SECTION_ID - 1];
}

static void prepare_channel_envelope(AcgcGxCanonicalEnvelope* envelope) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_CHANNEL_SECTION_MASK;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_CHANNEL_SECTION_MASK;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_CHANNEL_SECTION_BYTE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_CHANNEL_SECTION_BYTE_SIZE;

    entry = channel_entry(envelope);
    entry->section_version = ACGC_GX_CANONICAL_CHANNEL_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_CHANNEL_SECTION_BYTE_SIZE;
    entry->count = ACGC_GX_CANONICAL_CHANNEL_SECTION_COUNT;
    entry->capacity = ACGC_GX_CANONICAL_CHANNEL_SECTION_CAPACITY;
    entry->valid_mask = ACGC_GX_CANONICAL_CHANNEL_SECTION_MASK;
}

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static int accepts_exact_layout_and_values(void) {
    AcgcGxCanonicalChannelState state;

    CHECK(sizeof(AcgcGxCanonicalChannelState) == 136);
    CHECK(sizeof(AcgcGxCanonicalChannelRecord) == 64);
    CHECK(sizeof(AcgcGxCanonicalChannelControl) == 24);

    fill_valid_state(&state);
    CHECK(acgc_gx_canonical_channel_state_validate(&state));
    CHECK(state.records[0].ambient_rgba8 == UINT32_C(0x44332211));
    CHECK(state.records[0].material_rgba8 == UINT32_C(0x88776655));
    CHECK(state.records[1].color.material_source ==
          ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG);
    CHECK(state.records[1].alpha.material_source ==
          ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX);
    return 1;
}

static int accepts_empty_and_disabled_vtx_states(void) {
    AcgcGxCanonicalChannelState state;

    memset(&state, 0, sizeof(state));
    CHECK(acgc_gx_canonical_channel_state_validate(&state));

    state.active_count = 1;
    state.record_valid_mask = 1;
    state.records[0].channel_index = 0;
    fill_control(&state.records[0].color, 0, 1, 1, 0, 0, 2);
    fill_control(&state.records[0].alpha, 0, 1, 1, 0, 0, 2);
    CHECK(acgc_gx_canonical_channel_state_validate(&state));
    return 1;
}

static int rejects_unknown_relationships_and_nonzero_inactive_records(void) {
    AcgcGxCanonicalChannelState state;

    CHECK(!acgc_gx_canonical_channel_state_validate(NULL));

    fill_valid_state(&state);
    state.active_count = 3;
    CHECK(!acgc_gx_canonical_channel_state_validate(&state));

    fill_valid_state(&state);
    state.record_valid_mask = 2;
    CHECK(!acgc_gx_canonical_channel_state_validate(&state));

    fill_valid_state(&state);
    state.records[0].channel_index = 1;
    CHECK(!acgc_gx_canonical_channel_state_validate(&state));

    fill_valid_state(&state);
    state.records[1].reserved = 1;
    CHECK(!acgc_gx_canonical_channel_state_validate(&state));

    fill_valid_state(&state);
    state.active_count = 1;
    state.record_valid_mask = 1;
    CHECK(!acgc_gx_canonical_channel_state_validate(&state));

    memset(&state, 0, sizeof(state));
    state.records[1].material_rgba8 = 1;
    CHECK(acgc_gx_canonical_channel_state_validate(&state) == 0);
    return 1;
}

static int rejects_bad_domains_and_specular_diffuse(void) {
    AcgcGxCanonicalChannelState state;

    fill_valid_state(&state);
    state.records[0].color.enable = 2;
    CHECK(!acgc_gx_canonical_channel_state_validate(&state));

    fill_valid_state(&state);
    state.records[0].color.ambient_source = 2;
    CHECK(!acgc_gx_canonical_channel_state_validate(&state));

    fill_valid_state(&state);
    state.records[0].color.light_mask = UINT32_C(0x100);
    CHECK(!acgc_gx_canonical_channel_state_validate(&state));

    fill_valid_state(&state);
    state.records[0].color.diffuse_function = 3;
    CHECK(!acgc_gx_canonical_channel_state_validate(&state));

    fill_valid_state(&state);
    state.records[0].color.attenuation_function = 3;
    CHECK(!acgc_gx_canonical_channel_state_validate(&state));

    fill_valid_state(&state);
    state.records[0].color.attenuation_function =
        ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_SPEC;
    state.records[0].color.diffuse_function =
        ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_SIGN;
    CHECK(!acgc_gx_canonical_channel_state_validate(&state));
    return 1;
}

static int accepts_exact_and_absent_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_channel_envelope(&envelope);
    entry = channel_entry(&envelope);
    CHECK(acgc_gx_canonical_channel_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(entry->section_id == ACGC_GX_CANONICAL_CHANNEL_SECTION_ID);
    CHECK(entry->section_version == 1);
    CHECK(entry->byte_size == 136);
    CHECK(entry->count == 2);
    CHECK(entry->capacity == 2);
    CHECK(entry->valid_mask == UINT32_C(0x0004));
    CHECK(entry->reserved == 0);

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = channel_entry(&envelope);
    CHECK(acgc_gx_canonical_channel_metadata_validate(
        &envelope, sizeof(envelope)));
    CHECK(entry->section_id == ACGC_GX_CANONICAL_CHANNEL_SECTION_ID);
    CHECK(entry->section_version == 0);
    CHECK(entry->byte_offset == 0);
    CHECK(entry->byte_size == 0);
    CHECK(entry->count == 0);
    CHECK(entry->capacity == 0);
    CHECK(entry->valid_mask == 0);
    CHECK(entry->reserved == 0);
    return 1;
}

static int rejects_non_exact_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_channel_envelope(&envelope);
    entry = channel_entry(&envelope);
    entry->section_version = 2;
    CHECK(!acgc_gx_canonical_channel_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_channel_envelope(&envelope);
    entry = channel_entry(&envelope);
    entry->byte_size = 132;
    envelope.header.payload_byte_size = 132;
    envelope.header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 132;
    CHECK(!acgc_gx_canonical_channel_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_channel_envelope(&envelope);
    entry = channel_entry(&envelope);
    entry->count = 1;
    CHECK(!acgc_gx_canonical_channel_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_channel_envelope(&envelope);
    entry = channel_entry(&envelope);
    entry->capacity = 1;
    CHECK(!acgc_gx_canonical_channel_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_channel_envelope(&envelope);
    entry = channel_entry(&envelope);
    entry->valid_mask = UINT32_C(0x0002);
    CHECK(!acgc_gx_canonical_channel_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = channel_entry(&envelope);
    entry->reserved = 1;
    CHECK(!acgc_gx_canonical_channel_metadata_validate(
        &envelope, sizeof(envelope)));
    return 1;
}

static int encodes_exact_little_endian_field_order(void) {
    AcgcGxCanonicalChannelState state;
    uint8_t encoded[ACGC_GX_CANONICAL_CHANNEL_STATE_SIZE];
    uint32_t index;
    size_t offset = 8;

    fill_valid_state(&state);
    CHECK(acgc_gx_canonical_channel_state_encode(
        &state, encoded, sizeof(encoded)));
    CHECK(read_le32(encoded + 0) == state.active_count);
    CHECK(read_le32(encoded + 4) == state.record_valid_mask);

    for (index = 0;
         index < ACGC_GX_CANONICAL_CHANNEL_STATE_CAPACITY;
         index++) {
        const AcgcGxCanonicalChannelRecord* record = &state.records[index];
        const AcgcGxCanonicalChannelControl* controls[] = {
            &record->color, &record->alpha
        };
        uint32_t control_index;
        uint32_t field;

        CHECK(read_le32(encoded + offset) == record->channel_index);
        offset += 4;
        CHECK(read_le32(encoded + offset) == record->reserved);
        offset += 4;
        for (control_index = 0; control_index < 2; control_index++) {
            const AcgcGxCanonicalChannelControl* control =
                controls[control_index];
            const uint32_t values[] = {
                control->enable,
                control->ambient_source,
                control->material_source,
                control->light_mask,
                control->diffuse_function,
                control->attenuation_function
            };

            for (field = 0; field < 6; field++) {
                CHECK(read_le32(encoded + offset) == values[field]);
                offset += 4;
            }
        }
        CHECK(read_le32(encoded + offset) == record->ambient_rgba8);
        offset += 4;
        CHECK(read_le32(encoded + offset) == record->material_rgba8);
        offset += 4;
    }
    CHECK(offset == ACGC_GX_CANONICAL_CHANNEL_STATE_SIZE);
    CHECK(read_le32(encoded + 0x10) == 1);
    CHECK(read_le32(encoded + 0x40) == UINT32_C(0x44332211));
    CHECK(read_le32(encoded + 0x44) == UINT32_C(0x88776655));
    CHECK(read_le32(encoded + 0x48) == 1);
    return 1;
}

static int rejects_bad_encode_inputs_without_mutating_destination(void) {
    AcgcGxCanonicalChannelState state;
    uint8_t encoded[ACGC_GX_CANONICAL_CHANNEL_STATE_SIZE];
    uint8_t before[ACGC_GX_CANONICAL_CHANNEL_STATE_SIZE];

    fill_valid_state(&state);
    CHECK(!acgc_gx_canonical_channel_state_encode(
        NULL, encoded, sizeof(encoded)));
    CHECK(!acgc_gx_canonical_channel_state_encode(
        &state, NULL, sizeof(encoded)));

    memset(encoded, 0xA5, sizeof(encoded));
    memcpy(before, encoded, sizeof(before));
    CHECK(!acgc_gx_canonical_channel_state_encode(
        &state, encoded, sizeof(encoded) - 1));
    CHECK(memcmp(encoded, before, sizeof(encoded)) == 0);
    CHECK(!acgc_gx_canonical_channel_state_encode(
        &state, encoded, sizeof(encoded) + 1));
    CHECK(memcmp(encoded, before, sizeof(encoded)) == 0);

    state.active_count = 3;
    CHECK(!acgc_gx_canonical_channel_state_encode(
        &state, encoded, sizeof(encoded)));
    CHECK(memcmp(encoded, before, sizeof(encoded)) == 0);

    fill_valid_state(&state);
    state.records[0].color.enable = 2;
    CHECK(!acgc_gx_canonical_channel_state_encode(
        &state, encoded, sizeof(encoded)));
    CHECK(memcmp(encoded, before, sizeof(encoded)) == 0);

    fill_valid_state(&state);
    state.records[0].color.attenuation_function =
        ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_SPEC;
    state.records[0].color.diffuse_function =
        ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_SIGN;
    CHECK(!acgc_gx_canonical_channel_state_encode(
        &state, encoded, sizeof(encoded)));
    CHECK(memcmp(encoded, before, sizeof(encoded)) == 0);
    return 1;
}

int main(void) {
    if (!accepts_exact_layout_and_values() ||
        !accepts_empty_and_disabled_vtx_states() ||
        !rejects_unknown_relationships_and_nonzero_inactive_records() ||
        !rejects_bad_domains_and_specular_diffuse() ||
        !accepts_exact_and_absent_metadata() ||
        !rejects_non_exact_metadata() ||
        !encodes_exact_little_endian_field_order() ||
        !rejects_bad_encode_inputs_without_mutating_destination()) {
        return 1;
    }
    printf("GX canonical Channels tests: PASS\n");
    return 0;
}
