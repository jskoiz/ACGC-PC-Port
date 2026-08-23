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

static void fill_all_texture_records(AcgcGxCanonicalTextureState* state) {
    const uint32_t required_mask = UINT32_C(0x49);
    uint32_t map;

    memset(state, 0, sizeof(*state));
    fill_texture_header_metadata(&state->header);
    state->header.known_map_mask = UINT32_C(0xFF);
    state->header.known_map_count = 8;
    state->header.indexed_map_mask = UINT32_C(0xAA);
    state->header.mipmap_map_mask = UINT32_C(0x04);
    state->header.tlut_present_map_mask = UINT32_C(0xAA);
    state->header.required_map_mask = required_mask;

    for (map = 0; map < ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY; map++) {
        const int indexed = (map & 1) != 0;
        const int required = (required_mask & (UINT32_C(1) << map)) != 0;
        AcgcGxCanonicalTextureRecord* record = &state->records[map];

        fill_texture_record(record, map, indexed, required);
        record->image_owner_epoch = UINT32_C(0x30) + map;
        record->image_generation_lo = UINT32_C(0x100) + map;
        record->image_generation_hi = UINT32_C(0x200) + map;
        record->wrap_s = map % 3;
        record->wrap_t = (map + 1) % 3;
        record->min_filter = map % 6;
        record->mag_filter = map % 2;
        record->min_lod_q4 = map;
        record->max_lod_q4 = map * 2;
        record->lod_bias_q5 = map == 7 ? UINT32_C(0xFFFFFF80) : map;
        record->bias_clamp = map % 2;
        record->edge_lod = (map + 1) % 2;
        record->max_anisotropy = map % 3;
        record->image_byte_order = map % 2;
        record->image_source_kind = indexed ?
            ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_EMU64_CONVERTED :
            ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST;

        if (map == 2) {
            record->flags |= ACGC_GX_CANONICAL_TEXTURE_FLAG_MIPMAP;
            record->mip_level_count = 2;
            record->max_lod_q4 = 32;
            record->image_byte_size = 64;
        }
        if (indexed) {
            record->tlut_owner_epoch = UINT32_C(0x40) + map;
            record->tlut_generation_lo = UINT32_C(0x300) + map;
            record->tlut_generation_hi = UINT32_C(0x400) + map;
            record->tlut_name = map;
            record->tlut_resource_id =
                ACGC_GX_CANONICAL_TEXTURE_TLUT_RESOURCE_ID_BASE + map;
            record->tlut_entry_count = 16 + map;
            record->tlut_byte_size = (16 + map) * 2;
            record->tlut_byte_order = (map + 1) % 2;
            record->tlut_source_kind =
                map % 2 == 0 ?
                    ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST :
                    ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_EMU64_CONVERTED;
        }
    }
}

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static int texture_wire_words_match(
    const uint8_t* wire,
    const AcgcGxCanonicalTextureState* state
) {
    uint32_t expected_header[ACGC_GX_CANONICAL_TEXTURE_HEADER_WORD_COUNT];
    uint32_t expected_record[ACGC_GX_CANONICAL_TEXTURE_RECORD_WORD_COUNT];
    size_t offset = 0;
    uint32_t map;
    uint32_t word;

    expected_header[0] = state->header.known_map_mask;
    expected_header[1] = state->header.known_map_count;
    expected_header[2] = state->header.indexed_map_mask;
    expected_header[3] = state->header.mipmap_map_mask;
    expected_header[4] = state->header.tlut_present_map_mask;
    expected_header[5] = state->header.required_map_mask;
    expected_header[6] = state->header.record_byte_offset;
    expected_header[7] = state->header.record_count;
    expected_header[8] = state->header.record_capacity;
    expected_header[9] = state->header.record_word_count;
    expected_header[10] = state->header.resource_id_scheme;
    for (word = 0; word < 5; word++) {
        expected_header[11 + word] = state->header.reserved[word];
    }
    for (word = 0; word < ACGC_GX_CANONICAL_TEXTURE_HEADER_WORD_COUNT;
         word++) {
        if (read_le32(wire + offset) != expected_header[word]) {
            return 0;
        }
        offset += sizeof(uint32_t);
    }

    for (map = 0; map < ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY; map++) {
        const AcgcGxCanonicalTextureRecord* record = &state->records[map];

        expected_record[0] = record->flags;
        expected_record[1] = record->image_resource_id;
        expected_record[2] = record->image_owner_epoch;
        expected_record[3] = record->image_generation_lo;
        expected_record[4] = record->image_generation_hi;
        expected_record[5] = record->width;
        expected_record[6] = record->height;
        expected_record[7] = record->image_format;
        expected_record[8] = record->wrap_s;
        expected_record[9] = record->wrap_t;
        expected_record[10] = record->min_filter;
        expected_record[11] = record->mag_filter;
        expected_record[12] = record->min_lod_q4;
        expected_record[13] = record->max_lod_q4;
        expected_record[14] = record->lod_bias_q5;
        expected_record[15] = record->bias_clamp;
        expected_record[16] = record->edge_lod;
        expected_record[17] = record->max_anisotropy;
        expected_record[18] = record->mip_level_count;
        expected_record[19] = record->image_byte_size;
        expected_record[20] = record->image_byte_order;
        expected_record[21] = record->image_source_kind;
        expected_record[22] = record->tlut_resource_id;
        expected_record[23] = record->tlut_owner_epoch;
        expected_record[24] = record->tlut_generation_lo;
        expected_record[25] = record->tlut_generation_hi;
        expected_record[26] = record->tlut_name;
        expected_record[27] = record->tlut_format;
        expected_record[28] = record->tlut_entry_count;
        expected_record[29] = record->tlut_byte_size;
        expected_record[30] = record->tlut_byte_order;
        expected_record[31] = record->tlut_source_kind;
        for (word = 0; word < 4; word++) {
            expected_record[32 + word] = record->reserved[word];
        }
        for (word = 0; word < ACGC_GX_CANONICAL_TEXTURE_RECORD_WORD_COUNT;
             word++) {
            if (read_le32(wire + offset) != expected_record[word]) {
                return 0;
            }
            offset += sizeof(uint32_t);
        }
    }
    return offset == ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE;
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

static int encodes_all_texture_words_as_little_endian(void) {
    AcgcGxCanonicalTextureState state;
    uint8_t wire[ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE];
    uint8_t repeat[ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE];

    CHECK(sizeof(uint32_t) == 4);
    CHECK(sizeof(AcgcGxCanonicalTextureState) ==
        ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE);
    fill_all_texture_records(&state);
    CHECK(acgc_gx_canonical_texture_state_validate(&state));
    CHECK(acgc_gx_canonical_texture_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(texture_wire_words_match(wire, &state));
    CHECK(wire[0] == UINT8_C(0xFF));
    CHECK(wire[1] == UINT8_C(0x00));
    CHECK(wire[2] == UINT8_C(0x00));
    CHECK(wire[3] == UINT8_C(0x00));
    CHECK(wire[4] == UINT8_C(0x08));
    CHECK(wire[5] == UINT8_C(0x00));
    CHECK(wire[6] == UINT8_C(0x00));
    CHECK(wire[7] == UINT8_C(0x00));
    CHECK(read_le32(wire + 64 + 7 * 144 + 4) == 8);
    CHECK(read_le32(wire + 64 + 7 * 144 + 88) == UINT32_C(0x107));
    CHECK(read_le32(wire + 64 + 7 * 144 + 104) == 7);
    CHECK(read_le32(wire + 64 + 7 * 144 + 56) == UINT32_C(0xFFFFFF80));
    CHECK(acgc_gx_canonical_texture_state_encode(
        &state, repeat, sizeof(repeat)));
    CHECK(memcmp(wire, repeat, sizeof(wire)) == 0);
    return 1;
}

static int preserves_texture_output_on_encode_failure(void) {
    AcgcGxCanonicalTextureState state;
    uint8_t output[ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE];
    uint8_t before[ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE];

    fill_all_texture_records(&state);
    memset(output, 0xA5, sizeof(output));
    memcpy(before, output, sizeof(before));
    CHECK(!acgc_gx_canonical_texture_state_encode(
        NULL, output, sizeof(output)));
    CHECK(memcmp(output, before, sizeof(output)) == 0);
    CHECK(!acgc_gx_canonical_texture_state_encode(
        &state, output, sizeof(output) - 1));
    CHECK(memcmp(output, before, sizeof(output)) == 0);
    CHECK(!acgc_gx_canonical_texture_state_encode(
        &state, output, sizeof(output) + 1));
    CHECK(memcmp(output, before, sizeof(output)) == 0);
    CHECK(!acgc_gx_canonical_texture_state_encode(
        &state, NULL, sizeof(output)));

    state.header.reserved[0] = 1;
    CHECK(!acgc_gx_canonical_texture_state_encode(
        &state, output, sizeof(output)));
    CHECK(memcmp(output, before, sizeof(output)) == 0);

    fill_all_texture_records(&state);
    state.records[4].reserved[2] = 1;
    CHECK(!acgc_gx_canonical_texture_state_encode(
        &state, output, sizeof(output)));
    CHECK(memcmp(output, before, sizeof(output)) == 0);

    fill_all_texture_records(&state);
    state.records[6].image_resource_id = 0;
    CHECK(!acgc_gx_canonical_texture_state_encode(
        &state, output, sizeof(output)));
    CHECK(memcmp(output, before, sizeof(output)) == 0);
    return 1;
}

int main(void) {
    if (!accepts_exact_layout_and_value_records() ||
        !accepts_only_the_frozen_filter_domains() ||
        !accepts_exact_mip_sum_and_signed_q5() ||
        !rejects_masks_records_and_domains() ||
        !accepts_and_rejects_cross_resource_metadata() ||
        !accepts_exact_and_absent_metadata() ||
        !rejects_non_exact_metadata() ||
        !encodes_all_texture_words_as_little_endian() ||
        !preserves_texture_output_on_encode_failure()) {
        return 1;
    }
    printf("GX canonical Texture tests: PASS\n");
    return 0;
}
