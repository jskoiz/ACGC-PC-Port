#include "acgc/gx_canonical_texture_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static void fill_texture_header_metadata(
    AcgcGxCanonicalTextureHeader* header
) {
    header->record_byte_offset =
        ACGC_GX_CANONICAL_TEXTURE_HEADER_BYTE_SIZE;
    header->record_count = ACGC_GX_CANONICAL_TEXTURE_STATE_COUNT;
    header->record_capacity = ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY;
    header->record_word_count = ACGC_GX_CANONICAL_TEXTURE_RECORD_WORD_COUNT;
    header->resource_id_scheme =
        ACGC_GX_CANONICAL_TEXTURE_RESOURCE_ID_SCHEME;
}

static void fill_texture_record(
    AcgcGxCanonicalTextureRecord* record,
    uint32_t map,
    int indexed,
    int required
) {
    memset(record, 0, sizeof(*record));
    record->flags = (indexed ? ACGC_GX_CANONICAL_TEXTURE_FLAG_INDEXED : 0) |
        (indexed ? ACGC_GX_CANONICAL_TEXTURE_FLAG_TLUT_PRESENT : 0) |
        (required ? ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED : 0);
    record->image_resource_id =
        ACGC_GX_CANONICAL_TEXTURE_IMAGE_RESOURCE_ID_BASE + map;
    record->image_owner_epoch = 7;
    record->image_generation_lo = map == 0 ? 9 : 13;
    record->image_generation_hi = map == 0 ? 10 : 14;
    record->width = 8;
    record->height = indexed ? 8 : 4;
    record->image_format = indexed ?
        ACGC_GX_CANONICAL_TEXTURE_FORMAT_C4 :
        ACGC_GX_CANONICAL_TEXTURE_FORMAT_I8;
    record->wrap_s = ACGC_GX_CANONICAL_TEXTURE_WRAP_CLAMP;
    record->wrap_t = ACGC_GX_CANONICAL_TEXTURE_WRAP_CLAMP;
    record->min_filter = 0;
    record->mag_filter = 0;
    record->min_lod_q4 = 0;
    record->max_lod_q4 = 0;
    record->lod_bias_q5 = 0;
    record->bias_clamp = 0;
    record->edge_lod = 0;
    record->max_anisotropy = 0;
    record->mip_level_count = 1;
    record->image_byte_size = 32;
    record->image_byte_order = ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_GX_BE;
    record->image_source_kind =
        indexed ? ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_EMU64_CONVERTED :
        ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST;

    if (indexed) {
        record->tlut_resource_id =
            ACGC_GX_CANONICAL_TEXTURE_TLUT_RESOURCE_ID_BASE + 2;
        record->tlut_owner_epoch = 7;
        record->tlut_generation_lo = 11;
        record->tlut_generation_hi = 12;
        record->tlut_name = 2;
        record->tlut_format =
            ACGC_GX_CANONICAL_TEXTURE_TLUT_FORMAT_RGB5A3;
        record->tlut_entry_count = 16;
        record->tlut_byte_size = 32;
        record->tlut_byte_order = ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_LE;
        record->tlut_source_kind =
            ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_EMU64_CONVERTED;
    } else {
        record->tlut_name = ACGC_GX_CANONICAL_TEXTURE_TLUT_NAME_NONE;
    }
}

static void fill_texture_state(AcgcGxCanonicalTextureState* state) {
    memset(state, 0, sizeof(*state));
    fill_texture_header_metadata(&state->header);
    state->header.known_map_mask = UINT32_C(3);
    state->header.known_map_count = 2;
    state->header.indexed_map_mask = UINT32_C(2);
    state->header.mipmap_map_mask = 0;
    state->header.tlut_present_map_mask = UINT32_C(2);
    state->header.required_map_mask = UINT32_C(1);
    fill_texture_record(&state->records[0], 0, 0, 1);
    fill_texture_record(&state->records[1], 1, 1, 0);
}

static void fill_matching_dynamic(AcgcGxCanonicalDynamicState* state) {
    AcgcGxCanonicalDynamicRecord* image;
    AcgcGxCanonicalDynamicRecord* tlut;

    memset(state, 0, sizeof(*state));
    state->header.record_byte_offset =
        ACGC_GX_CANONICAL_DYNAMIC_HEADER_BYTE_SIZE;
    state->header.record_count = ACGC_GX_CANONICAL_DYNAMIC_STATE_COUNT;
    state->header.record_capacity = ACGC_GX_CANONICAL_DYNAMIC_STATE_CAPACITY;
    state->header.record_word_count =
        ACGC_GX_CANONICAL_DYNAMIC_RECORD_WORD_COUNT;
    state->header.resource_id_scheme =
        ACGC_GX_CANONICAL_DYNAMIC_RESOURCE_ID_SCHEME;
    state->header.owner_epoch = 7;
    state->header.present_image_mask = 1;
    state->header.present_tlut_mask = UINT32_C(1) << 2;
    state->header.required_image_mask = 1;
    state->header.required_tlut_mask = 0;
    state->header.present_resource_count = 2;

    image = &state->records[0];
    image->resource_id = 1;
    image->kind = ACGC_GX_CANONICAL_DYNAMIC_KIND_IMAGE;
    image->owner_epoch = 7;
    image->generation_lo = 9;
    image->generation_hi = 10;
    image->owner_slot = 0;
    image->byte_flags = ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED;
    image->byte_size = 32;
    image->byte_order = ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_GX_BE;
    image->alignment = ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES;
    image->source_kind =
        ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_RAW_GUEST;
    image->format = ACGC_GX_CANONICAL_DYNAMIC_FORMAT_I8;

    tlut = &state->records[8 + 2];
    tlut->resource_id = UINT32_C(0x102);
    tlut->kind = ACGC_GX_CANONICAL_DYNAMIC_KIND_TLUT;
    tlut->owner_epoch = 7;
    tlut->generation_lo = 11;
    tlut->generation_hi = 12;
    tlut->owner_slot = 2;
    tlut->byte_flags = ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_OWNED;
    tlut->byte_size = 32;
    tlut->byte_order = ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_LE;
    tlut->alignment = ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES;
    tlut->source_kind =
        ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_EMU64_CONVERTED;
    tlut->format = ACGC_GX_CANONICAL_DYNAMIC_TLUT_FORMAT_RGB5A3;
    tlut->element_count = 16;
}

static void fill_matching_dynamic_image_one(
    AcgcGxCanonicalDynamicRecord* image
) {
    memset(image, 0, sizeof(*image));
    image->resource_id = 2;
    image->kind = ACGC_GX_CANONICAL_DYNAMIC_KIND_IMAGE;
    image->owner_epoch = 7;
    image->generation_lo = 13;
    image->generation_hi = 14;
    image->owner_slot = 1;
    image->byte_flags = ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED;
    image->byte_size = 32;
    image->byte_order = ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_GX_BE;
    image->alignment = ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES;
    image->source_kind =
        ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_EMU64_CONVERTED;
    image->format = ACGC_GX_CANONICAL_DYNAMIC_FORMAT_C4;
}

static void prepare_texture_envelope(
    AcgcGxCanonicalEnvelope* envelope
) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_TEXTURE_SECTION_MASK;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_TEXTURE_SECTION_MASK;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_TEXTURE_SECTION_BYTE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_TEXTURE_SECTION_BYTE_SIZE;

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_TEXTURE_SECTION_ID - 1];
    entry->section_version = ACGC_GX_CANONICAL_TEXTURE_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_TEXTURE_SECTION_BYTE_SIZE;
    entry->count = ACGC_GX_CANONICAL_TEXTURE_SECTION_COUNT;
    entry->capacity = ACGC_GX_CANONICAL_TEXTURE_SECTION_CAPACITY;
    entry->valid_mask = ACGC_GX_CANONICAL_TEXTURE_SECTION_MASK;
}

static int accepts_exact_layout_and_value_records(void) {
    AcgcGxCanonicalTextureState state;

    CHECK(sizeof(AcgcGxCanonicalTextureHeader) == 64);
    CHECK(sizeof(AcgcGxCanonicalTextureRecord) == 144);
    CHECK(sizeof(AcgcGxCanonicalTextureState) == 1216);

    fill_texture_state(&state);
    CHECK(acgc_gx_canonical_texture_state_validate(&state));
    CHECK(state.records[0].image_resource_id == 1);
    CHECK(state.records[1].tlut_resource_id == UINT32_C(0x102));
    CHECK(state.records[1].tlut_name == 2);
    return 1;
}

static int accepts_only_the_frozen_filter_domains(void) {
    AcgcGxCanonicalTextureState state;

    fill_texture_state(&state);
    state.records[0].min_filter = 5;
    state.records[0].mag_filter = 0;
    CHECK(acgc_gx_canonical_texture_state_validate(&state));

    state.records[0].mag_filter = 1;
    CHECK(acgc_gx_canonical_texture_state_validate(&state));

    state.records[0].mag_filter = 2;
    CHECK(!acgc_gx_canonical_texture_state_validate(&state));
    return 1;
}

static int accepts_exact_mip_sum_and_signed_q5(void) {
    AcgcGxCanonicalTextureState state;

    memset(&state, 0, sizeof(state));
    fill_texture_header_metadata(&state.header);
    state.header.known_map_mask = 1;
    state.header.known_map_count = 1;
    state.header.mipmap_map_mask = 1;
    fill_texture_record(&state.records[0], 0, 0, 0);
    state.records[0].flags = ACGC_GX_CANONICAL_TEXTURE_FLAG_MIPMAP;
    state.records[0].mip_level_count = 4;
    state.records[0].image_byte_size = 128;
    state.records[0].max_lod_q4 = 64;
    state.records[0].lod_bias_q5 = UINT32_C(0xFFFFFF80);
    CHECK(acgc_gx_canonical_texture_state_validate(&state));

    state.records[0].image_byte_size = 96;
    CHECK(!acgc_gx_canonical_texture_state_validate(&state));

    state.records[0].image_byte_size = 128;
    state.records[0].lod_bias_q5 = UINT32_C(0xFFFFFF7F);
    CHECK(!acgc_gx_canonical_texture_state_validate(&state));
    return 1;
}

static int rejects_masks_records_and_domains(void) {
    AcgcGxCanonicalTextureState state;

    fill_texture_state(&state);
    state.header.known_map_count = 1;
    CHECK(!acgc_gx_canonical_texture_state_validate(&state));

    fill_texture_state(&state);
    state.records[2].image_resource_id = 3;
    CHECK(!acgc_gx_canonical_texture_state_validate(&state));

    fill_texture_state(&state);
    state.records[0].width = 0;
    CHECK(!acgc_gx_canonical_texture_state_validate(&state));

    fill_texture_state(&state);
    state.records[0].image_format = 7;
    CHECK(!acgc_gx_canonical_texture_state_validate(&state));

    fill_texture_state(&state);
    state.records[0].tlut_name = 0;
    CHECK(!acgc_gx_canonical_texture_state_validate(&state));

    fill_texture_state(&state);
    state.records[1].tlut_entry_count = 0;
    CHECK(!acgc_gx_canonical_texture_state_validate(&state));

    fill_texture_state(&state);
    state.header.indexed_map_mask = 0;
    CHECK(!acgc_gx_canonical_texture_state_validate(&state));
    return 1;
}

static int accepts_and_rejects_cross_resource_metadata(void) {
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalDynamicState dynamic;

    fill_texture_state(&texture);
    fill_matching_dynamic(&dynamic);
    CHECK(acgc_gx_canonical_texture_dynamic_validate(&texture, &dynamic));

    dynamic.records[0].generation_lo++;
    CHECK(!acgc_gx_canonical_texture_dynamic_validate(&texture, &dynamic));

    fill_matching_dynamic(&dynamic);
    dynamic.records[0].byte_flags =
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED;
    CHECK(!acgc_gx_canonical_texture_dynamic_validate(&texture, &dynamic));

    fill_matching_dynamic(&dynamic);
    dynamic.header.required_image_mask = 0;
    CHECK(!acgc_gx_canonical_texture_dynamic_validate(&texture, &dynamic));

    fill_matching_dynamic(&dynamic);
    dynamic.records[10].format =
        ACGC_GX_CANONICAL_DYNAMIC_TLUT_FORMAT_IA8;
    CHECK(!acgc_gx_canonical_texture_dynamic_validate(&texture, &dynamic));

    fill_texture_state(&texture);
    texture.header.required_map_mask = UINT32_C(3);
    texture.records[1].flags |=
        ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED;
    fill_matching_dynamic(&dynamic);
    dynamic.header.present_image_mask = UINT32_C(3);
    dynamic.header.required_image_mask = UINT32_C(3);
    dynamic.header.required_tlut_mask = UINT32_C(1) << 2;
    dynamic.header.present_resource_count = 3;
    fill_matching_dynamic_image_one(&dynamic.records[1]);
    CHECK(acgc_gx_canonical_texture_dynamic_validate(&texture, &dynamic));

    dynamic.header.required_tlut_mask = UINT32_C(1) << 1;
    CHECK(!acgc_gx_canonical_texture_dynamic_validate(&texture, &dynamic));
    return 1;
}

static int accepts_exact_and_absent_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_texture_envelope(&envelope);
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_TEXTURE_SECTION_ID - 1];
    CHECK(acgc_gx_canonical_texture_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(entry->section_id == ACGC_GX_CANONICAL_TEXTURE_SECTION_ID);
    CHECK(entry->section_version == 1);
    CHECK(entry->byte_size == 1216);
    CHECK(entry->count == 8);
    CHECK(entry->capacity == 8);
    CHECK(entry->valid_mask == UINT32_C(0x0010));
    CHECK(entry->reserved == 0);

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_TEXTURE_SECTION_ID - 1];
    CHECK(acgc_gx_canonical_texture_metadata_validate(
        &envelope, sizeof(envelope)));
    CHECK(entry->section_id == ACGC_GX_CANONICAL_TEXTURE_SECTION_ID);
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

    prepare_texture_envelope(&envelope);
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_TEXTURE_SECTION_ID - 1];
    entry->section_version = 2;
    CHECK(!acgc_gx_canonical_texture_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_texture_envelope(&envelope);
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_TEXTURE_SECTION_ID - 1];
    entry->capacity = 7;
    CHECK(!acgc_gx_canonical_texture_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    return 1;
}

int main(void) {
    if (!accepts_exact_layout_and_value_records() ||
        !accepts_only_the_frozen_filter_domains() ||
        !accepts_exact_mip_sum_and_signed_q5() ||
        !rejects_masks_records_and_domains() ||
        !accepts_and_rejects_cross_resource_metadata() ||
        !accepts_exact_and_absent_metadata() ||
        !rejects_non_exact_metadata()) {
        return 1;
    }
    printf("GX canonical Texture tests: PASS\n");
    return 0;
}
