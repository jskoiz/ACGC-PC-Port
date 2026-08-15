#include "acgc/gx_canonical_indirect_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", \
                    __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static void fill_indirect_header(
    AcgcGxCanonicalIndirectState* state,
    uint32_t active_count,
    uint32_t matrix_valid_mask
) {
    memset(state, 0, sizeof(*state));
    state->header.version = ACGC_GX_CANONICAL_INDIRECT_STATE_VERSION;
    state->header.section_id = ACGC_GX_CANONICAL_INDIRECT_SECTION_ID;
    state->header.section_mask = ACGC_GX_CANONICAL_INDIRECT_SECTION_MASK;
    state->header.byte_size = ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE;
    state->header.active_indirect_stage_count = active_count;
    state->header.order_capacity =
        ACGC_GX_CANONICAL_INDIRECT_ORDER_CAPACITY;
    state->header.order_record_size =
        ACGC_GX_CANONICAL_INDIRECT_ORDER_RECORD_SIZE;
    state->header.order_offset = ACGC_GX_CANONICAL_INDIRECT_ORDER_OFFSET;
    state->header.active_order_mask = active_count == 0 ? 0 :
        (UINT32_C(1) << active_count) - UINT32_C(1);
    state->header.matrix_capacity =
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_CAPACITY;
    state->header.matrix_record_size =
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_RECORD_SIZE;
    state->header.matrix_offset = ACGC_GX_CANONICAL_INDIRECT_MATRIX_OFFSET;
    state->header.matrix_valid_mask = matrix_valid_mask;
}

static void fill_indirect_state(AcgcGxCanonicalIndirectState* state) {
    fill_indirect_header(state, 2, 1);

    state->orders[0].tex_coord = 0;
    state->orders[0].tex_map = 1;
    state->orders[0].scale_s = 8;
    state->orders[0].scale_t = 1;
    state->orders[1].tex_coord = 1;
    state->orders[1].tex_map = 2;
    state->orders[1].scale_s = 8;
    state->orders[1].scale_t = 1;

    state->matrices[0].s0 = 1023;
    state->matrices[0].t0 = -1024;
    state->matrices[0].s1 = 0;
    state->matrices[0].t1 = 1;
    state->matrices[0].s2 = 2;
    state->matrices[0].t2 = -3;
    state->matrices[0].encoded_scale = 63;
}

static void fill_texture_record(
    AcgcGxCanonicalTextureRecord* record,
    uint32_t map
) {
    memset(record, 0, sizeof(*record));
    record->image_resource_id =
        ACGC_GX_CANONICAL_TEXTURE_IMAGE_RESOURCE_ID_BASE + map;
    record->image_owner_epoch = 1;
    record->image_generation_lo = 2 + map;
    record->image_generation_hi = 3 + map;
    record->width = 8;
    record->height = 4;
    record->image_format = ACGC_GX_CANONICAL_TEXTURE_FORMAT_I8;
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
    record->image_byte_order =
        ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_GX_BE;
    record->image_source_kind =
        ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST;
    record->tlut_name = ACGC_GX_CANONICAL_TEXTURE_TLUT_NAME_NONE;
}

static void fill_texture_state(AcgcGxCanonicalTextureState* state) {
    memset(state, 0, sizeof(*state));
    state->header.known_map_mask = UINT32_C(0x06);
    state->header.known_map_count = 2;
    state->header.indexed_map_mask = 0;
    state->header.mipmap_map_mask = 0;
    state->header.tlut_present_map_mask = 0;
    state->header.required_map_mask = 0;
    state->header.record_byte_offset =
        ACGC_GX_CANONICAL_TEXTURE_HEADER_BYTE_SIZE;
    state->header.record_count = ACGC_GX_CANONICAL_TEXTURE_STATE_COUNT;
    state->header.record_capacity =
        ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY;
    state->header.record_word_count =
        ACGC_GX_CANONICAL_TEXTURE_RECORD_WORD_COUNT;
    state->header.resource_id_scheme =
        ACGC_GX_CANONICAL_TEXTURE_RESOURCE_ID_SCHEME;
    fill_texture_record(&state->records[1], 1);
    fill_texture_record(&state->records[2], 2);
}

static void fill_tev_stage_direct(AcgcGxCanonicalTevStage* stage) {
    memset(stage, 0, sizeof(*stage));
}

static void fill_tev_state(AcgcGxCanonicalTevState* state) {
    uint32_t index;

    memset(state, 0, sizeof(*state));
    state->header.version = ACGC_GX_CANONICAL_TEV_STATE_VERSION;
    state->header.section_id = ACGC_GX_CANONICAL_TEV_SECTION_ID;
    state->header.section_mask = ACGC_GX_CANONICAL_TEV_SECTION_MASK;
    state->header.byte_size = ACGC_GX_CANONICAL_TEV_STATE_SIZE;
    state->header.active_stage_count = 2;
    state->header.stage_capacity = ACGC_GX_CANONICAL_TEV_STAGE_CAPACITY;
    state->header.component_valid_mask =
        ACGC_GX_CANONICAL_TEV_COMPONENT_VALID_MASK;
    state->header.stage_offset = ACGC_GX_CANONICAL_TEV_STAGE_OFFSET;
    state->header.stage_record_size =
        ACGC_GX_CANONICAL_TEV_STAGE_RECORD_SIZE;
    state->header.register_offset = ACGC_GX_CANONICAL_TEV_REGISTER_OFFSET;
    state->header.register_record_size =
        ACGC_GX_CANONICAL_TEV_REGISTER_RECORD_SIZE;
    state->header.konst_offset = ACGC_GX_CANONICAL_TEV_KONST_OFFSET;
    state->header.konst_record_size =
        ACGC_GX_CANONICAL_TEV_KONST_RECORD_SIZE;
    state->header.swap_table_offset =
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_OFFSET;
    state->header.swap_table_record_size =
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_RECORD_SIZE;

    fill_tev_stage_direct(&state->stages[0]);
    state->stages[0].tex_coord = 0;
    state->stages[0].tex_map = 0;
    state->stages[1] = state->stages[0];
    state->stages[1].tex_coord = 1;
    state->stages[1].ind_stage = 1;
    state->stages[1].ind_mtx = ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_0;

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        state->registers[index].r = 0;
        state->registers[index].g = 0;
        state->registers[index].b = 0;
        state->registers[index].a = 0;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        state->konst[index].r = 0;
        state->konst[index].g = 0;
        state->konst[index].b = 0;
        state->konst[index].a = 0;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT; index++) {
        state->swap_tables[index].r = 0;
        state->swap_tables[index].g = 0;
        state->swap_tables[index].b = 0;
        state->swap_tables[index].a = 0;
    }
}

static void fill_geometry_dependencies(
    AcgcGxCanonicalGeometryDependencyResults* dependencies
) {
    memset(dependencies, 0, sizeof(*dependencies));
    dependencies->texgens_valid = 1;
    dependencies->texgen_present_mask = UINT32_C(0x03);
    dependencies->texgen_ordinary_known_mask = UINT32_C(0x03);
    dependencies->texgen_selector[0] =
        ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_FIRST;
    dependencies->texgen_selector[1] =
        ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_FIRST +
            ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_STRIDE;
}

static void put_le32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)(value & UINT32_C(0xFF));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    destination[3] = (uint8_t)((value >> 24) & UINT32_C(0xFF));
}

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static uint32_t state_word_at(
    const AcgcGxCanonicalIndirectState* state,
    uint32_t index
) {
    uint32_t word;

    memcpy(&word, ((const uint8_t*)state) + index * sizeof(uint32_t),
           sizeof(word));
    return word;
}

static void encode_state_le(
    const AcgcGxCanonicalIndirectState* state,
    uint8_t* bytes
) {
    uint32_t index;

    for (index = 0;
         index < ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE / sizeof(uint32_t);
         index++) {
        put_le32(bytes + index * sizeof(uint32_t), state_word_at(state, index));
    }
}

static void decode_state_le(
    const uint8_t* bytes,
    AcgcGxCanonicalIndirectState* state
) {
    uint32_t index;
    uint32_t word;

    memset(state, 0, sizeof(*state));
    for (index = 0;
         index < ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE / sizeof(uint32_t);
         index++) {
        word = read_le32(bytes + index * sizeof(uint32_t));
        memcpy(((uint8_t*)state) + index * sizeof(uint32_t), &word,
               sizeof(word));
    }
}

static AcgcGxCanonicalEnvelopeDirectoryEntry* indirect_entry(
    AcgcGxCanonicalEnvelope* envelope
) {
    return &envelope->directory[
        ACGC_GX_CANONICAL_INDIRECT_SECTION_ID - 1];
}

static void prepare_indirect_envelope(
    AcgcGxCanonicalEnvelope* envelope
) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_INDIRECT_SECTION_MASK;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_INDIRECT_SECTION_MASK;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_INDIRECT_SECTION_BYTE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_INDIRECT_SECTION_BYTE_SIZE;

    entry = indirect_entry(envelope);
    entry->section_version = ACGC_GX_CANONICAL_INDIRECT_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_INDIRECT_SECTION_BYTE_SIZE;
    entry->count = ACGC_GX_CANONICAL_INDIRECT_SECTION_COUNT;
    entry->capacity = ACGC_GX_CANONICAL_INDIRECT_SECTION_CAPACITY;
    entry->valid_mask = ACGC_GX_CANONICAL_INDIRECT_SECTION_MASK;
}

static int accepts_layout_and_boundaries(void) {
    AcgcGxCanonicalIndirectState state;

    CHECK(sizeof(AcgcGxCanonicalIndirectHeader) == 56);
    CHECK(sizeof(AcgcGxCanonicalIndirectOrder) == 24);
    CHECK(sizeof(AcgcGxCanonicalIndirectMatrix) == 32);
    CHECK(sizeof(AcgcGxCanonicalIndirectState) == 248);

    fill_indirect_state(&state);
    CHECK(acgc_gx_canonical_indirect_state_validate(&state));

    fill_indirect_header(&state, 0, 0);
    CHECK(acgc_gx_canonical_indirect_state_validate(&state));

    fill_indirect_header(&state, 4, 1);
    state.orders[0].tex_coord = 7;
    state.orders[0].tex_map = 7;
    state.orders[0].scale_s = 0;
    state.orders[0].scale_t = 8;
    state.orders[1] = state.orders[0];
    state.orders[2] = state.orders[0];
    state.orders[3] = state.orders[0];
    state.matrices[0].s0 = -1024;
    state.matrices[0].t0 = 1023;
    state.matrices[0].s1 = -1024;
    state.matrices[0].t1 = 1023;
    state.matrices[0].s2 = -1024;
    state.matrices[0].t2 = 1023;
    state.matrices[0].encoded_scale = 0;
    CHECK(acgc_gx_canonical_indirect_state_validate(&state));

    fill_indirect_header(&state, 5, 0);
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    return 1;
}

static int rejects_malformed_metadata_and_reserved_words(void) {
    AcgcGxCanonicalIndirectState state;

    fill_indirect_state(&state);
    state.header.version = 2;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.header.section_id = 12;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.header.section_mask = ACGC_GX_CANONICAL_SECTION_MASK_TEV;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.header.byte_size = 244;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.header.active_order_mask = 1;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.header.order_capacity = 3;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.header.order_record_size = 20;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.header.order_offset = 60;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.header.matrix_capacity = 2;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.header.matrix_record_size = 28;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.header.matrix_offset = 156;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.header.matrix_valid_mask = UINT32_C(8);
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.header.reserved = 1;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    CHECK(!acgc_gx_canonical_indirect_state_validate(NULL));
    return 1;
}

static int rejects_value_domains_and_inactive_records(void) {
    AcgcGxCanonicalIndirectState state;

    fill_indirect_state(&state);
    state.orders[0].tex_coord = 8;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.orders[0].tex_map = 8;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.orders[0].scale_s = 9;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.orders[0].scale_t = 9;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.orders[0].reserved[0] = 1;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.orders[2].tex_map = 1;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));

    fill_indirect_state(&state);
    state.matrices[0].s0 = -1025;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.matrices[0].t2 = 1024;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.matrices[0].encoded_scale = 64;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.matrices[0].reserved = 1;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    fill_indirect_state(&state);
    state.matrices[1].encoded_scale = 1;
    CHECK(!acgc_gx_canonical_indirect_state_validate(&state));
    return 1;
}

static int accepts_exact_metadata_and_absent_entry(void) {
    AcgcGxCanonicalEnvelope envelope;

    prepare_indirect_envelope(&envelope);
    CHECK(acgc_gx_canonical_indirect_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    (void)acgc_gx_canonical_envelope_init(&envelope);
    CHECK(acgc_gx_canonical_indirect_metadata_validate(
        &envelope, ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET));

    prepare_indirect_envelope(&envelope);
    indirect_entry(&envelope)->count = 2;
    CHECK(!acgc_gx_canonical_indirect_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_indirect_envelope(&envelope);
    indirect_entry(&envelope)->byte_size = 244;
    CHECK(!acgc_gx_canonical_indirect_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    return 1;
}

static int round_trips_explicit_little_endian_words(void) {
    AcgcGxCanonicalIndirectState state;
    AcgcGxCanonicalIndirectState decoded;
    uint8_t wire[ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE];
    uint8_t roundtrip[ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE];
    uint32_t index;

    fill_indirect_state(&state);
    encode_state_le(&state, wire);
    CHECK(read_le32(wire + 0) == 1);
    CHECK(read_le32(wire + 4) == 13);
    CHECK(read_le32(wire + 8) == UINT32_C(0x1000));
    CHECK(read_le32(wire + 12) == 248);
    for (index = 0;
         index < ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE / sizeof(uint32_t);
         index++) {
        CHECK(read_le32(wire + index * sizeof(uint32_t)) ==
            state_word_at(&state, index));
    }

    decode_state_le(wire, &decoded);
    CHECK(memcmp(&state, &decoded, sizeof(state)) == 0);
    encode_state_le(&decoded, roundtrip);
    CHECK(memcmp(wire, roundtrip, sizeof(wire)) == 0);
    CHECK(acgc_gx_canonical_indirect_state_validate(&decoded));
    return 1;
}

static int accepts_and_rejects_cross_section_dependencies(void) {
    AcgcGxCanonicalIndirectState state;
    AcgcGxCanonicalTevState tev;
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalGeometryDependencyResults geometry;

    fill_indirect_state(&state);
    fill_tev_state(&tev);
    fill_texture_state(&texture);
    fill_geometry_dependencies(&geometry);
    CHECK(acgc_gx_canonical_indirect_state_validate_dependencies(
        &state, &tev, &texture, &geometry));
    CHECK(acgc_gx_canonical_indirect_state_validate_dependencies(
        &state, &tev, NULL, NULL));

    tev.stages[1].ind_stage = 2;
    CHECK(!acgc_gx_canonical_indirect_state_validate_dependencies(
        &state, &tev, &texture, &geometry));

    fill_tev_state(&tev);
    state.header.matrix_valid_mask = 0;
    memset(&state.matrices[0], 0, sizeof(state.matrices[0]));
    CHECK(!acgc_gx_canonical_indirect_state_validate_dependencies(
        &state, &tev, &texture, &geometry));

    fill_indirect_state(&state);
    fill_tev_state(&tev);
    memset(&texture.records[2], 0, sizeof(texture.records[2]));
    texture.header.known_map_mask = UINT32_C(0x02);
    texture.header.known_map_count = 1;
    CHECK(!acgc_gx_canonical_indirect_state_validate_dependencies(
        &state, &tev, &texture, &geometry));

    fill_texture_state(&texture);
    geometry.texgen_present_mask = UINT32_C(0x01);
    geometry.texgen_selector[1] = 0;
    CHECK(!acgc_gx_canonical_indirect_state_validate_dependencies(
        &state, &tev, &texture, &geometry));

    fill_geometry_dependencies(&geometry);
    tev.stages[0].tex_map = 1;
    CHECK(!acgc_gx_canonical_indirect_state_validate_dependencies(
        &state, &tev, &texture, &geometry));

    CHECK(!acgc_gx_canonical_indirect_state_validate_dependencies(
        NULL, &tev, NULL, NULL));
    CHECK(!acgc_gx_canonical_indirect_state_validate_dependencies(
        &state, NULL, NULL, NULL));
    return 1;
}

int main(void) {
    CHECK(accepts_layout_and_boundaries());
    CHECK(rejects_malformed_metadata_and_reserved_words());
    CHECK(rejects_value_domains_and_inactive_records());
    CHECK(accepts_exact_metadata_and_absent_entry());
    CHECK(round_trips_explicit_little_endian_words());
    CHECK(accepts_and_rejects_cross_section_dependencies());
    puts("canonical indirect state tests passed");
    return 0;
}
