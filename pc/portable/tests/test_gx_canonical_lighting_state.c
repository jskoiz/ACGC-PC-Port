#include "acgc/gx_canonical_lighting_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static void fill_valid_record(
    AcgcGxCanonicalLightingRecord* record,
    uint32_t color_rgba8,
    uint32_t seed
) {
    memset(record, 0, sizeof(*record));
    record->color_rgba8 = color_rgba8;
    record->angular_attenuation[0] = UINT32_C(0x3F800000);
    record->angular_attenuation[1] = UINT32_C(0xBF800000);
    record->angular_attenuation[2] = seed;
    record->distance_attenuation[0] = UINT32_C(0x3F000000);
    record->distance_attenuation[1] = UINT32_C(0x00000000);
    record->distance_attenuation[2] = UINT32_C(0x3E800000);
    record->position[0] = seed;
    record->position[1] = UINT32_C(0xC0000000);
    record->position[2] = UINT32_C(0x40400000);
    /* Zero direction is valid and must not be normalized or rejected. */
}

static void set_float_word(
    AcgcGxCanonicalLightingRecord* record,
    uint32_t vector,
    uint32_t index,
    uint32_t bits
) {
    switch (vector) {
        case 0:
            record->angular_attenuation[index] = bits;
            break;
        case 1:
            record->distance_attenuation[index] = bits;
            break;
        case 2:
            record->position[index] = bits;
            break;
        default:
            record->direction[index] = bits;
            break;
    }
}

static AcgcGxCanonicalEnvelopeDirectoryEntry* lighting_entry(
    AcgcGxCanonicalEnvelope* envelope
) {
    return &envelope->directory[
        ACGC_GX_CANONICAL_LIGHTING_SECTION_ID - 1];
}

static void prepare_lighting_envelope(AcgcGxCanonicalEnvelope* envelope) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_LIGHTING_SECTION_MASK;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_LIGHTING_SECTION_MASK;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_LIGHTING_SECTION_BYTE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_LIGHTING_SECTION_BYTE_SIZE;

    entry = lighting_entry(envelope);
    entry->section_version = ACGC_GX_CANONICAL_LIGHTING_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_LIGHTING_SECTION_BYTE_SIZE;
    entry->count = ACGC_GX_CANONICAL_LIGHTING_SECTION_COUNT;
    entry->capacity = ACGC_GX_CANONICAL_LIGHTING_SECTION_CAPACITY;
    entry->valid_mask = ACGC_GX_CANONICAL_LIGHTING_SECTION_MASK;
}

static int accepts_exact_layout_and_all_eight_slots(void) {
    AcgcGxCanonicalLightingState state;
    uint32_t slot;

    CHECK(sizeof(AcgcGxCanonicalLightingRecord) == 64);
    CHECK(sizeof(AcgcGxCanonicalLightingState) == 516);

    memset(&state, 0, sizeof(state));
    for (slot = 0; slot < ACGC_GX_CANONICAL_LIGHTING_SLOT_COUNT; slot++) {
        state.loaded_mask |= UINT32_C(1) << slot;
        fill_valid_record(
            &state.records[slot],
            UINT32_C(0x40302010) + slot,
            UINT32_C(0x3F000000) + (slot << 16));
    }
    CHECK(state.loaded_mask == UINT32_C(0xFF));
    CHECK(state.records[0].color_rgba8 == UINT32_C(0x40302010));
    CHECK(state.records[7].color_rgba8 == UINT32_C(0x40302017));
    CHECK(acgc_gx_canonical_lighting_state_validate(&state));
    return 1;
}

static int accepts_empty_single_slots_and_zero_direction(void) {
    AcgcGxCanonicalLightingState state;
    uint32_t slot;

    memset(&state, 0, sizeof(state));
    CHECK(acgc_gx_canonical_lighting_state_validate(&state));

    for (slot = 0; slot < ACGC_GX_CANONICAL_LIGHTING_SLOT_COUNT; slot++) {
        memset(&state, 0, sizeof(state));
        state.loaded_mask = UINT32_C(1) << slot;
        fill_valid_record(&state.records[slot], UINT32_C(0x44332211), 0);
        CHECK(acgc_gx_canonical_lighting_state_validate(&state));
    }
    return 1;
}

static int rejects_bad_masks_unloaded_values_and_reserved_words(void) {
    AcgcGxCanonicalLightingState state;

    memset(&state, 0, sizeof(state));
    state.loaded_mask = UINT32_C(0x100);
    CHECK(!acgc_gx_canonical_lighting_state_validate(&state));

    memset(&state, 0, sizeof(state));
    state.loaded_mask = 1;
    fill_valid_record(&state.records[0], UINT32_C(0x44332211), 0);
    state.records[1].color_rgba8 = 1;
    CHECK(!acgc_gx_canonical_lighting_state_validate(&state));

    memset(&state, 0, sizeof(state));
    state.records[0].color_rgba8 = 1;
    CHECK(!acgc_gx_canonical_lighting_state_validate(&state));

    for (uint32_t index = 0;
         index < ACGC_GX_CANONICAL_LIGHTING_RESERVED_WORD_COUNT;
         index++) {
        memset(&state, 0, sizeof(state));
        state.loaded_mask = 1;
        fill_valid_record(&state.records[0], UINT32_C(0x44332211), 0);
        state.records[0].reserved[index] = 1;
        CHECK(!acgc_gx_canonical_lighting_state_validate(&state));
    }
    return 1;
}

static int rejects_every_nonfinite_float_word(void) {
    AcgcGxCanonicalLightingState state;

    for (uint32_t vector = 0; vector < 4; vector++) {
        for (uint32_t index = 0;
             index < ACGC_GX_CANONICAL_LIGHTING_FLOAT_VECTOR_COUNT;
             index++) {
            memset(&state, 0, sizeof(state));
            state.loaded_mask = 1;
            fill_valid_record(&state.records[0], UINT32_C(0x44332211), 0);
            set_float_word(
                &state.records[0], vector, index, UINT32_C(0x7F800000));
            CHECK(!acgc_gx_canonical_lighting_state_validate(&state));

            memset(&state, 0, sizeof(state));
            state.loaded_mask = 1;
            fill_valid_record(&state.records[0], UINT32_C(0x44332211), 0);
            set_float_word(
                &state.records[0], vector, index, UINT32_C(0x7FC00001));
            CHECK(!acgc_gx_canonical_lighting_state_validate(&state));
        }
    }
    return 1;
}

static int accepts_exact_and_absent_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_lighting_envelope(&envelope);
    entry = lighting_entry(&envelope);
    CHECK(acgc_gx_canonical_lighting_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(entry->section_id == ACGC_GX_CANONICAL_LIGHTING_SECTION_ID);
    CHECK(entry->section_version == 1);
    CHECK(entry->byte_size == 516);
    CHECK(entry->count == 8);
    CHECK(entry->capacity == 8);
    CHECK(entry->valid_mask == UINT32_C(0x0040));
    CHECK(entry->reserved == 0);

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = lighting_entry(&envelope);
    CHECK(acgc_gx_canonical_lighting_metadata_validate(
        &envelope, sizeof(envelope)));
    CHECK(entry->section_id == ACGC_GX_CANONICAL_LIGHTING_SECTION_ID);
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

    prepare_lighting_envelope(&envelope);
    entry = lighting_entry(&envelope);
    entry->section_version = 2;
    CHECK(!acgc_gx_canonical_lighting_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_lighting_envelope(&envelope);
    entry = lighting_entry(&envelope);
    entry->byte_size = 512;
    envelope.header.payload_byte_size = 512;
    envelope.header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 512;
    CHECK(!acgc_gx_canonical_lighting_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_lighting_envelope(&envelope);
    entry = lighting_entry(&envelope);
    entry->count = 7;
    CHECK(!acgc_gx_canonical_lighting_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_lighting_envelope(&envelope);
    entry = lighting_entry(&envelope);
    entry->capacity = 7;
    CHECK(!acgc_gx_canonical_lighting_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_lighting_envelope(&envelope);
    entry = lighting_entry(&envelope);
    entry->valid_mask = UINT32_C(0x0020);
    CHECK(!acgc_gx_canonical_lighting_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = lighting_entry(&envelope);
    entry->reserved = 1;
    CHECK(!acgc_gx_canonical_lighting_metadata_validate(
        &envelope, sizeof(envelope)));
    return 1;
}

int main(void) {
    if (!accepts_exact_layout_and_all_eight_slots() ||
        !accepts_empty_single_slots_and_zero_direction() ||
        !rejects_bad_masks_unloaded_values_and_reserved_words() ||
        !rejects_every_nonfinite_float_word() ||
        !accepts_exact_and_absent_metadata() ||
        !rejects_non_exact_metadata()) {
        return 1;
    }
    printf("GX canonical Lighting tests: PASS\n");
    return 0;
}
