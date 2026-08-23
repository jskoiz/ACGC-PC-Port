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

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
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

static int encodes_wire_layout_and_preserves_output(void) {
    AcgcGxCanonicalIndirectState state;
    uint8_t wire[ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE];
    uint8_t roundtrip[ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE];
    uint8_t before[ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE];
    uint32_t index;
    uint32_t word;

    fill_indirect_state(&state);
    CHECK(acgc_gx_canonical_indirect_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(read_le32(wire + 0) == 1);
    CHECK(read_le32(wire + 4) == 13);
    CHECK(read_le32(wire + 8) == UINT32_C(0x1000));
    CHECK(read_le32(wire + 12) == 248);
    CHECK(read_le32(wire + 16) == 2);
    CHECK(read_le32(wire + 20) == 4);
    CHECK(read_le32(wire + 24) == 24);
    CHECK(read_le32(wire + 28) == 56);
    CHECK(read_le32(wire + 32) == 3);
    CHECK(read_le32(wire + 36) == 3);
    CHECK(read_le32(wire + 40) == 32);
    CHECK(read_le32(wire + 44) == 152);
    CHECK(read_le32(wire + 48) == 1);
    CHECK(read_le32(wire + 52) == 0);

    for (index = 0; index < ACGC_GX_CANONICAL_INDIRECT_ORDER_COUNT; index++) {
        const AcgcGxCanonicalIndirectOrder* order = &state.orders[index];
        const size_t offset =
            ACGC_GX_CANONICAL_INDIRECT_ORDER_OFFSET +
            index * ACGC_GX_CANONICAL_INDIRECT_ORDER_RECORD_SIZE;

        CHECK(read_le32(wire + offset + 0) == order->tex_coord);
        CHECK(read_le32(wire + offset + 4) == order->tex_map);
        CHECK(read_le32(wire + offset + 8) == order->scale_s);
        CHECK(read_le32(wire + offset + 12) == order->scale_t);
        CHECK(read_le32(wire + offset + 16) == order->reserved[0]);
        CHECK(read_le32(wire + offset + 20) == order->reserved[1]);
    }
    for (index = 0; index < ACGC_GX_CANONICAL_INDIRECT_MATRIX_COUNT; index++) {
        const AcgcGxCanonicalIndirectMatrix* matrix = &state.matrices[index];
        const size_t offset =
            ACGC_GX_CANONICAL_INDIRECT_MATRIX_OFFSET +
            index * ACGC_GX_CANONICAL_INDIRECT_MATRIX_RECORD_SIZE;

        CHECK(read_le32(wire + offset + 0) == (uint32_t)matrix->s0);
        CHECK(read_le32(wire + offset + 4) == (uint32_t)matrix->t0);
        CHECK(read_le32(wire + offset + 8) == (uint32_t)matrix->s1);
        CHECK(read_le32(wire + offset + 12) == (uint32_t)matrix->t1);
        CHECK(read_le32(wire + offset + 16) == (uint32_t)matrix->s2);
        CHECK(read_le32(wire + offset + 20) == (uint32_t)matrix->t2);
        CHECK(read_le32(wire + offset + 24) == matrix->encoded_scale);
        CHECK(read_le32(wire + offset + 28) == matrix->reserved);
    }

    CHECK(acgc_gx_canonical_indirect_state_encode(
        &state, roundtrip, sizeof(roundtrip)));
    CHECK(memcmp(wire, roundtrip, sizeof(wire)) == 0);

    fill_indirect_header(&state, 0, 0);
    CHECK(acgc_gx_canonical_indirect_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(read_le32(wire + 16) == 0);
    CHECK(read_le32(wire + 32) == 0);
    for (word = ACGC_GX_CANONICAL_INDIRECT_ORDER_OFFSET;
         word < ACGC_GX_CANONICAL_INDIRECT_MATRIX_OFFSET;
         word += sizeof(uint32_t)) {
        CHECK(read_le32(wire + word) == 0);
    }
    for (word = ACGC_GX_CANONICAL_INDIRECT_MATRIX_OFFSET;
         word < ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE;
         word += sizeof(uint32_t)) {
        CHECK(read_le32(wire + word) == 0);
    }

    fill_indirect_header(&state, 4, 7);
    for (index = 0; index < ACGC_GX_CANONICAL_INDIRECT_ORDER_COUNT; index++) {
        state.orders[index].tex_coord = index;
        state.orders[index].tex_map = 7 - index;
        state.orders[index].scale_s = index;
        state.orders[index].scale_t = 8 - index;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_INDIRECT_MATRIX_COUNT; index++) {
        state.matrices[index].s0 = (int32_t)index;
        state.matrices[index].t0 = -(int32_t)index;
        state.matrices[index].s1 = 1;
        state.matrices[index].t1 = -1;
        state.matrices[index].s2 = 2;
        state.matrices[index].t2 = -2;
        state.matrices[index].encoded_scale = index * 21;
    }
    CHECK(acgc_gx_canonical_indirect_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(read_le32(wire + 16) == 4);
    CHECK(read_le32(wire + 32) == UINT32_C(0xF));
    CHECK(read_le32(wire + 56 + 3 * 24 + 12) == 5);
    CHECK(read_le32(wire + 152 + 2 * 32 + 24) == 42);

    memset(wire, 0xA5, sizeof(wire));
    memcpy(before, wire, sizeof(before));
    CHECK(!acgc_gx_canonical_indirect_state_encode(NULL, wire, sizeof(wire)));
    CHECK(memcmp(wire, before, sizeof(wire)) == 0);
    fill_indirect_state(&state);
    CHECK(!acgc_gx_canonical_indirect_state_encode(
        &state, wire, sizeof(wire) - 1));
    CHECK(memcmp(wire, before, sizeof(wire)) == 0);
    state.header.active_indirect_stage_count = 5;
    CHECK(!acgc_gx_canonical_indirect_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(memcmp(wire, before, sizeof(wire)) == 0);
    fill_indirect_state(&state);
    state.header.reserved = 1;
    CHECK(!acgc_gx_canonical_indirect_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(memcmp(wire, before, sizeof(wire)) == 0);
    fill_indirect_state(&state);
    state.orders[2].tex_coord = 1;
    CHECK(!acgc_gx_canonical_indirect_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(memcmp(wire, before, sizeof(wire)) == 0);
    fill_indirect_state(&state);
    state.matrices[0].reserved = 1;
    CHECK(!acgc_gx_canonical_indirect_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(memcmp(wire, before, sizeof(wire)) == 0);
    CHECK(!acgc_gx_canonical_indirect_state_encode(
        &state, NULL, sizeof(wire)));
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
    CHECK(encodes_wire_layout_and_preserves_output());
    CHECK(accepts_and_rejects_cross_section_dependencies());
    puts("canonical indirect state tests passed");
    return 0;
}
