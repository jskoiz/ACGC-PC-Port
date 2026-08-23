#include "acgc/gx_canonical_dynamic_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static void fill_dynamic_header_metadata(
    AcgcGxCanonicalDynamicHeader* header
) {
    header->record_byte_offset =
        ACGC_GX_CANONICAL_DYNAMIC_HEADER_BYTE_SIZE;
    header->record_count = ACGC_GX_CANONICAL_DYNAMIC_STATE_COUNT;
    header->record_capacity = ACGC_GX_CANONICAL_DYNAMIC_STATE_CAPACITY;
    header->record_word_count = ACGC_GX_CANONICAL_DYNAMIC_RECORD_WORD_COUNT;
    header->resource_id_scheme =
        ACGC_GX_CANONICAL_DYNAMIC_RESOURCE_ID_SCHEME;
}

static void fill_image_record(
    AcgcGxCanonicalDynamicRecord* record,
    uint32_t owner_epoch,
    uint32_t generation_lo,
    uint32_t generation_hi
) {
    memset(record, 0, sizeof(*record));
    record->resource_id = ACGC_GX_CANONICAL_DYNAMIC_IMAGE_RESOURCE_ID_BASE;
    record->kind = ACGC_GX_CANONICAL_DYNAMIC_KIND_IMAGE;
    record->owner_epoch = owner_epoch;
    record->generation_lo = generation_lo;
    record->generation_hi = generation_hi;
    record->owner_slot = 0;
    record->byte_flags = ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED;
    record->byte_size = 32;
    record->byte_order = ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_GX_BE;
    record->alignment = ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES;
    record->source_kind = ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_RAW_GUEST;
    record->format = ACGC_GX_CANONICAL_DYNAMIC_FORMAT_I8;
}

static void fill_tlut_record(
    AcgcGxCanonicalDynamicRecord* record,
    uint32_t slot,
    uint32_t owner_epoch,
    uint32_t generation_lo,
    uint32_t generation_hi
) {
    memset(record, 0, sizeof(*record));
    record->resource_id =
        ACGC_GX_CANONICAL_DYNAMIC_TLUT_RESOURCE_ID_BASE + slot;
    record->kind = ACGC_GX_CANONICAL_DYNAMIC_KIND_TLUT;
    record->owner_epoch = owner_epoch;
    record->generation_lo = generation_lo;
    record->generation_hi = generation_hi;
    record->owner_slot = slot;
    record->byte_flags = ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_OWNED;
    record->byte_size = 32;
    record->byte_order = ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_LE;
    record->alignment = ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES;
    record->source_kind =
        ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_EMU64_CONVERTED;
    record->format = ACGC_GX_CANONICAL_DYNAMIC_TLUT_FORMAT_RGB5A3;
    record->element_count = 16;
}

static void fill_all_dynamic_records(AcgcGxCanonicalDynamicState* state) {
    const uint32_t required_image_mask = UINT32_C(0xA5);
    const uint32_t required_tlut_mask = UINT32_C(0xA55A);
    uint32_t slot;

    memset(state, 0, sizeof(*state));
    fill_dynamic_header_metadata(&state->header);
    state->header.owner_epoch = UINT32_C(0x50);
    state->header.present_image_mask = UINT32_C(0xFF);
    state->header.present_tlut_mask = UINT32_C(0xFFFF);
    state->header.required_image_mask = required_image_mask;
    state->header.required_tlut_mask = required_tlut_mask;
    state->header.present_resource_count = 24;

    for (slot = 0;
         slot < ACGC_GX_CANONICAL_DYNAMIC_IMAGE_MAP_COUNT;
         slot++) {
        AcgcGxCanonicalDynamicRecord* record = &state->records[slot];

        fill_image_record(
            record, state->header.owner_epoch,
            UINT32_C(0x100) + slot, UINT32_C(0x200) + slot);
        record->resource_id =
            ACGC_GX_CANONICAL_DYNAMIC_IMAGE_RESOURCE_ID_BASE + slot;
        record->owner_slot = slot;
        record->byte_flags = ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
            (slot % 2 == 0 ? ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED :
                ACGC_GX_CANONICAL_DYNAMIC_BYTES_OWNED);
        record->byte_order = slot % 2;
        record->source_kind = slot % 2 == 0 ?
            ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_RAW_GUEST :
            ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_EMU64_CONVERTED;
        record->format = slot % 7;
    }
    for (slot = 0;
         slot < ACGC_GX_CANONICAL_DYNAMIC_TLUT_SLOT_COUNT;
         slot++) {
        AcgcGxCanonicalDynamicRecord* record =
            &state->records[
                ACGC_GX_CANONICAL_DYNAMIC_IMAGE_MAP_COUNT + slot];

        fill_tlut_record(
            record, slot, state->header.owner_epoch,
            UINT32_C(0x300) + slot, UINT32_C(0x400) + slot);
        record->byte_flags = ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
            (slot % 2 == 0 ? ACGC_GX_CANONICAL_DYNAMIC_BYTES_OWNED :
                ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED);
        record->byte_order = slot % 2;
        record->source_kind = slot % 2 == 0 ?
            ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_EMU64_CONVERTED :
            ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_RAW_GUEST;
        record->format = slot % 3;
        record->element_count = 16 + slot;
        record->byte_size = record->element_count * 2;
    }
}

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static int dynamic_wire_words_match(
    const uint8_t* wire,
    const AcgcGxCanonicalDynamicState* state
) {
    uint32_t expected_header[ACGC_GX_CANONICAL_DYNAMIC_HEADER_WORD_COUNT];
    uint32_t expected_record[ACGC_GX_CANONICAL_DYNAMIC_RECORD_WORD_COUNT];
    size_t offset = 0;
    uint32_t record_index;
    uint32_t word;

    expected_header[0] = state->header.owner_epoch;
    expected_header[1] = state->header.present_image_mask;
    expected_header[2] = state->header.present_tlut_mask;
    expected_header[3] = state->header.required_image_mask;
    expected_header[4] = state->header.required_tlut_mask;
    expected_header[5] = state->header.present_resource_count;
    expected_header[6] = state->header.record_byte_offset;
    expected_header[7] = state->header.record_count;
    expected_header[8] = state->header.record_capacity;
    expected_header[9] = state->header.record_word_count;
    expected_header[10] = state->header.resource_id_scheme;
    for (word = 0; word < 5; word++) {
        expected_header[11 + word] = state->header.reserved[word];
    }
    for (word = 0; word < ACGC_GX_CANONICAL_DYNAMIC_HEADER_WORD_COUNT;
         word++) {
        if (read_le32(wire + offset) != expected_header[word]) {
            return 0;
        }
        offset += sizeof(uint32_t);
    }

    for (record_index = 0;
         record_index < ACGC_GX_CANONICAL_DYNAMIC_STATE_CAPACITY;
         record_index++) {
        const AcgcGxCanonicalDynamicRecord* record =
            &state->records[record_index];

        expected_record[0] = record->resource_id;
        expected_record[1] = record->kind;
        expected_record[2] = record->owner_epoch;
        expected_record[3] = record->generation_lo;
        expected_record[4] = record->generation_hi;
        expected_record[5] = record->owner_slot;
        expected_record[6] = record->byte_flags;
        expected_record[7] = record->byte_size;
        expected_record[8] = record->byte_order;
        expected_record[9] = record->alignment;
        expected_record[10] = record->source_kind;
        expected_record[11] = record->format;
        expected_record[12] = record->element_count;
        for (word = 0; word < 3; word++) {
            expected_record[13 + word] = record->reserved[word];
        }
        for (word = 0; word < ACGC_GX_CANONICAL_DYNAMIC_RECORD_WORD_COUNT;
             word++) {
            if (read_le32(wire + offset) != expected_record[word]) {
                return 0;
            }
            offset += sizeof(uint32_t);
        }
    }
    return offset == ACGC_GX_CANONICAL_DYNAMIC_STATE_SIZE;
}

static void prepare_dynamic_envelope(
    AcgcGxCanonicalEnvelope* envelope
) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_MASK;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_MASK;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_BYTE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_BYTE_SIZE;

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID - 1];
    entry->section_version = ACGC_GX_CANONICAL_DYNAMIC_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_DYNAMIC_SECTION_BYTE_SIZE;
    entry->count = ACGC_GX_CANONICAL_DYNAMIC_SECTION_COUNT;
    entry->capacity = ACGC_GX_CANONICAL_DYNAMIC_SECTION_CAPACITY;
    entry->valid_mask = ACGC_GX_CANONICAL_DYNAMIC_SECTION_MASK;
}

static int accepts_exact_layout_and_empty_epoch(void) {
    AcgcGxCanonicalDynamicState state;

    CHECK(sizeof(AcgcGxCanonicalDynamicHeader) == 64);
    CHECK(sizeof(AcgcGxCanonicalDynamicRecord) == 64);
    CHECK(sizeof(AcgcGxCanonicalDynamicState) == 1600);

    memset(&state, 0, sizeof(state));
    fill_dynamic_header_metadata(&state.header);
    state.header.owner_epoch = 7;
    CHECK(acgc_gx_canonical_dynamic_state_validate(&state));
    return 1;
}

static int accepts_image_and_tlut_records(void) {
    AcgcGxCanonicalDynamicState state;

    memset(&state, 0, sizeof(state));
    fill_dynamic_header_metadata(&state.header);
    state.header.owner_epoch = 7;
    state.header.present_image_mask = UINT32_C(1);
    state.header.present_tlut_mask = UINT32_C(1) << 2;
    state.header.required_image_mask = UINT32_C(1);
    state.header.required_tlut_mask = UINT32_C(1) << 2;
    state.header.present_resource_count = 2;
    fill_image_record(&state.records[0], 7, 9, 10);
    fill_tlut_record(&state.records[8 + 2], 2, 7, 11, 12);

    CHECK(acgc_gx_canonical_dynamic_state_validate(&state));
    CHECK(state.records[0].resource_id == 1);
    CHECK(state.records[10].resource_id == UINT32_C(0x102));
    CHECK(state.records[10].element_count == 16);
    return 1;
}

static int rejects_masks_presence_and_absent_records(void) {
    AcgcGxCanonicalDynamicState state;

    memset(&state, 0, sizeof(state));
    state.header.owner_epoch = 7;
    state.header.present_image_mask = UINT32_C(0x100);
    CHECK(!acgc_gx_canonical_dynamic_state_validate(&state));

    memset(&state, 0, sizeof(state));
    state.header.owner_epoch = 7;
    state.header.present_image_mask = 1;
    state.header.present_resource_count = 0;
    fill_image_record(&state.records[0], 7, 1, 0);
    CHECK(!acgc_gx_canonical_dynamic_state_validate(&state));

    memset(&state, 0, sizeof(state));
    state.header.owner_epoch = 7;
    state.records[1].resource_id = 2;
    CHECK(!acgc_gx_canonical_dynamic_state_validate(&state));

    memset(&state, 0, sizeof(state));
    CHECK(!acgc_gx_canonical_dynamic_state_validate(&state));
    return 1;
}

static int rejects_record_domains_and_lease_rules(void) {
    AcgcGxCanonicalDynamicState state;

    memset(&state, 0, sizeof(state));
    state.header.owner_epoch = 7;
    state.header.present_image_mask = 1;
    state.header.present_resource_count = 1;
    fill_image_record(&state.records[0], 7, 1, 0);
    state.records[0].byte_flags = ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED;
    CHECK(!acgc_gx_canonical_dynamic_state_validate(&state));

    state.records[0].byte_flags =
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED |
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_OWNED;
    CHECK(!acgc_gx_canonical_dynamic_state_validate(&state));

    fill_image_record(&state.records[0], 7, 1, 0);
    state.records[0].alignment = 16;
    CHECK(!acgc_gx_canonical_dynamic_state_validate(&state));

    fill_image_record(&state.records[0], 7, 1, 0);
    state.records[0].format = 7;
    CHECK(!acgc_gx_canonical_dynamic_state_validate(&state));

    fill_tlut_record(&state.records[0], 0, 7, 1, 0);
    CHECK(!acgc_gx_canonical_dynamic_state_validate(&state));

    memset(&state, 0, sizeof(state));
    state.header.owner_epoch = 7;
    state.header.present_tlut_mask = 1;
    state.header.present_resource_count = 1;
    fill_tlut_record(&state.records[8], 0, 7, 1, 0);
    state.records[8].byte_size = 30;
    CHECK(!acgc_gx_canonical_dynamic_state_validate(&state));
    return 1;
}

static int accepts_exact_and_absent_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_dynamic_envelope(&envelope);
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID - 1];
    CHECK(acgc_gx_canonical_dynamic_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(entry->section_id == ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID);
    CHECK(entry->section_version == 1);
    CHECK(entry->byte_size == 1600);
    CHECK(entry->count == 24);
    CHECK(entry->capacity == 24);
    CHECK(entry->valid_mask == UINT32_C(0x2000));
    CHECK(entry->reserved == 0);

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID - 1];
    CHECK(acgc_gx_canonical_dynamic_metadata_validate(
        &envelope, sizeof(envelope)));
    CHECK(entry->section_id == ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID);
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

    prepare_dynamic_envelope(&envelope);
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID - 1];
    entry->section_version = 2;
    CHECK(!acgc_gx_canonical_dynamic_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_dynamic_envelope(&envelope);
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID - 1];
    entry->byte_size = 1596;
    CHECK(!acgc_gx_canonical_dynamic_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_dynamic_envelope(&envelope);
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID - 1];
    entry->count = 23;
    CHECK(!acgc_gx_canonical_dynamic_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID - 1];
    entry->reserved = 1;
    CHECK(!acgc_gx_canonical_dynamic_metadata_validate(
        &envelope, sizeof(envelope)));
    return 1;
}

static int encodes_all_dynamic_words_as_little_endian(void) {
    AcgcGxCanonicalDynamicState state;
    uint8_t wire[ACGC_GX_CANONICAL_DYNAMIC_STATE_SIZE];
    uint8_t repeat[ACGC_GX_CANONICAL_DYNAMIC_STATE_SIZE];

    CHECK(sizeof(uint32_t) == 4);
    CHECK(sizeof(AcgcGxCanonicalDynamicState) ==
        ACGC_GX_CANONICAL_DYNAMIC_STATE_SIZE);
    fill_all_dynamic_records(&state);
    CHECK(acgc_gx_canonical_dynamic_state_validate(&state));
    CHECK(acgc_gx_canonical_dynamic_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(dynamic_wire_words_match(wire, &state));
    CHECK(wire[0] == UINT8_C(0x50));
    CHECK(wire[1] == UINT8_C(0x00));
    CHECK(wire[2] == UINT8_C(0x00));
    CHECK(wire[3] == UINT8_C(0x00));
    CHECK(wire[4] == UINT8_C(0xFF));
    CHECK(wire[5] == UINT8_C(0x00));
    CHECK(wire[6] == UINT8_C(0x00));
    CHECK(wire[7] == UINT8_C(0x00));
    CHECK(read_le32(wire + 64 + 7 * 64) == 8);
    CHECK(read_le32(wire + 64 + 8 * 64) == UINT32_C(0x100));
    CHECK(read_le32(wire + 64 + 23 * 64 + 5 * 4) == 15);
    CHECK(read_le32(wire + 64 + 23 * 64 + 48) == 31);
    CHECK(acgc_gx_canonical_dynamic_state_encode(
        &state, repeat, sizeof(repeat)));
    CHECK(memcmp(wire, repeat, sizeof(wire)) == 0);
    return 1;
}

static int preserves_dynamic_output_on_encode_failure(void) {
    AcgcGxCanonicalDynamicState state;
    uint8_t output[ACGC_GX_CANONICAL_DYNAMIC_STATE_SIZE];
    uint8_t before[ACGC_GX_CANONICAL_DYNAMIC_STATE_SIZE];

    fill_all_dynamic_records(&state);
    memset(output, 0xA5, sizeof(output));
    memcpy(before, output, sizeof(before));
    CHECK(!acgc_gx_canonical_dynamic_state_encode(
        NULL, output, sizeof(output)));
    CHECK(memcmp(output, before, sizeof(output)) == 0);
    CHECK(!acgc_gx_canonical_dynamic_state_encode(
        &state, output, sizeof(output) - 1));
    CHECK(memcmp(output, before, sizeof(output)) == 0);
    CHECK(!acgc_gx_canonical_dynamic_state_encode(
        &state, output, sizeof(output) + 1));
    CHECK(memcmp(output, before, sizeof(output)) == 0);
    CHECK(!acgc_gx_canonical_dynamic_state_encode(
        &state, NULL, sizeof(output)));

    state.header.reserved[0] = 1;
    CHECK(!acgc_gx_canonical_dynamic_state_encode(
        &state, output, sizeof(output)));
    CHECK(memcmp(output, before, sizeof(output)) == 0);

    fill_all_dynamic_records(&state);
    state.records[12].reserved[1] = 1;
    CHECK(!acgc_gx_canonical_dynamic_state_encode(
        &state, output, sizeof(output)));
    CHECK(memcmp(output, before, sizeof(output)) == 0);

    fill_all_dynamic_records(&state);
    state.records[19].resource_id = 0;
    CHECK(!acgc_gx_canonical_dynamic_state_encode(
        &state, output, sizeof(output)));
    CHECK(memcmp(output, before, sizeof(output)) == 0);
    return 1;
}

int main(void) {
    if (!accepts_exact_layout_and_empty_epoch() ||
        !accepts_image_and_tlut_records() ||
        !rejects_masks_presence_and_absent_records() ||
        !rejects_record_domains_and_lease_rules() ||
        !accepts_exact_and_absent_metadata() ||
        !rejects_non_exact_metadata() ||
        !encodes_all_dynamic_words_as_little_endian() ||
        !preserves_dynamic_output_on_encode_failure()) {
        return 1;
    }
    printf("GX canonical Dynamic tests: PASS\n");
    return 0;
}
