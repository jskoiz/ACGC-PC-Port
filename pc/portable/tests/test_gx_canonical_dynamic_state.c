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

int main(void) {
    if (!accepts_exact_layout_and_empty_epoch() ||
        !accepts_image_and_tlut_records() ||
        !rejects_masks_presence_and_absent_records() ||
        !rejects_record_domains_and_lease_rules() ||
        !accepts_exact_and_absent_metadata() ||
        !rejects_non_exact_metadata()) {
        return 1;
    }
    printf("GX canonical Dynamic tests: PASS\n");
    return 0;
}
