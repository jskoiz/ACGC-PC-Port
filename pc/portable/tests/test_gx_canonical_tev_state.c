#include "acgc/gx_canonical_tev_state.h"

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

static void fill_stage(AcgcGxCanonicalTevStage* stage, uint32_t index) {
    memset(stage, 0, sizeof(*stage));
    stage->color_a = 15;
    stage->color_b = index % 16;
    stage->color_c = 14;
    stage->color_d = 12;
    stage->alpha_a = 7;
    stage->alpha_b = index % 8;
    stage->alpha_c = 6;
    stage->alpha_d = 4;
    stage->color_op = (index == 15) ? 15 : 0;
    stage->color_bias = 2;
    stage->color_scale = 3;
    stage->color_clamp = 1;
    stage->color_out = 3;
    stage->alpha_op = (index == 14) ? 8 : 1;
    stage->alpha_bias = 2;
    stage->alpha_scale = 3;
    stage->alpha_clamp = 1;
    stage->alpha_out = 2;
    stage->tex_coord = 7;
    stage->tex_map = 7;
    stage->color_chan = 8;
    stage->k_color_sel = 31;
    stage->k_alpha_sel = 31;
    stage->ras_swap = 3;
    stage->tex_swap = 3;
    stage->ind_stage = 3;
    stage->ind_format = 3;
    stage->ind_bias = 7;
    stage->ind_mtx = 11;
    stage->ind_wrap_s = 6;
    stage->ind_wrap_t = 6;
    stage->ind_add_prev = 1;
    stage->ind_lod = 1;
    stage->ind_alpha = 3;
}

static void fill_state(
    AcgcGxCanonicalTevState* state,
    uint32_t active_stage_count
) {
    uint32_t index;

    memset(state, 0, sizeof(*state));
    state->header.version = ACGC_GX_CANONICAL_TEV_STATE_VERSION;
    state->header.section_id = ACGC_GX_CANONICAL_TEV_SECTION_ID;
    state->header.section_mask = ACGC_GX_CANONICAL_TEV_SECTION_MASK;
    state->header.byte_size = ACGC_GX_CANONICAL_TEV_STATE_SIZE;
    state->header.active_stage_count = active_stage_count;
    state->header.stage_capacity = ACGC_GX_CANONICAL_TEV_STAGE_CAPACITY;
    state->header.component_valid_mask =
        ACGC_GX_CANONICAL_TEV_COMPONENT_VALID_MASK;
    state->header.stage_offset = ACGC_GX_CANONICAL_TEV_STAGE_OFFSET;
    state->header.stage_record_size =
        ACGC_GX_CANONICAL_TEV_STAGE_RECORD_SIZE;
    state->header.register_offset =
        ACGC_GX_CANONICAL_TEV_REGISTER_OFFSET;
    state->header.register_record_size =
        ACGC_GX_CANONICAL_TEV_REGISTER_RECORD_SIZE;
    state->header.konst_offset = ACGC_GX_CANONICAL_TEV_KONST_OFFSET;
    state->header.konst_record_size =
        ACGC_GX_CANONICAL_TEV_KONST_RECORD_SIZE;
    state->header.swap_table_offset =
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_OFFSET;
    state->header.swap_table_record_size =
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_RECORD_SIZE;

    for (index = 0; index < active_stage_count &&
         index < ACGC_GX_CANONICAL_TEV_STAGE_COUNT; index++) {
        fill_stage(&state->stages[index], index);
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        state->registers[index].r = -1024;
        state->registers[index].g = 1023;
        state->registers[index].b = -1024 + (int32_t)index;
        state->registers[index].a = 1023 - (int32_t)index;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        state->konst[index].r = 0;
        state->konst[index].g = 255;
        state->konst[index].b = index;
        state->konst[index].a = 255 - index;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT; index++) {
        state->swap_tables[index].r = 0;
        state->swap_tables[index].g = 1;
        state->swap_tables[index].b = 2;
        state->swap_tables[index].a = 3;
    }
}

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static int expect_stage_word_rejected(
    AcgcGxCanonicalTevState* state,
    uint32_t* word,
    uint32_t value
) {
    const uint32_t original = *word;
    int rejected;

    *word = value;
    rejected = !acgc_gx_canonical_tev_state_validate(state);
    *word = original;
    return rejected;
}

static int expect_register_value_rejected(
    AcgcGxCanonicalTevState* state,
    int32_t* value,
    int32_t replacement
) {
    const int32_t original = *value;
    int rejected;

    *value = replacement;
    rejected = !acgc_gx_canonical_tev_state_validate(state);
    *value = original;
    return rejected;
}

static AcgcGxCanonicalEnvelopeDirectoryEntry* tev_entry(
    AcgcGxCanonicalEnvelope* envelope
) {
    return &envelope->directory[ACGC_GX_CANONICAL_TEV_SECTION_ID - 1];
}

static void prepare_tev_envelope(
    AcgcGxCanonicalEnvelope* envelope,
    uint32_t active_stage_count
) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_TEV_SECTION_MASK;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_TEV_SECTION_MASK;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_TEV_SECTION_BYTE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_TEV_SECTION_BYTE_SIZE;

    entry = tev_entry(envelope);
    entry->section_version = ACGC_GX_CANONICAL_TEV_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_TEV_SECTION_BYTE_SIZE;
    entry->count = active_stage_count;
    entry->capacity = ACGC_GX_CANONICAL_TEV_SECTION_CAPACITY;
    entry->valid_mask = ACGC_GX_CANONICAL_TEV_SECTION_MASK;
}

static int accepts_layout_boundaries_and_all_stages(void) {
    AcgcGxCanonicalTevState state;
    uint32_t index;

    CHECK(sizeof(AcgcGxCanonicalTevState) == 2560);
    CHECK(sizeof(AcgcGxCanonicalTevStage) == 144);
    CHECK(sizeof(AcgcGxCanonicalTevRegister) == 16);
    CHECK(sizeof(AcgcGxCanonicalTevKonst) == 16);
    CHECK(sizeof(AcgcGxCanonicalTevSwapTable) == 16);

    for (index = ACGC_GX_CANONICAL_TEV_ACTIVE_STAGE_COUNT_MIN;
         index <= ACGC_GX_CANONICAL_TEV_ACTIVE_STAGE_COUNT_MAX; index++) {
        fill_state(&state, index);
        CHECK(acgc_gx_canonical_tev_state_validate(&state));
    }
    CHECK(state.stages[15].color_op == 15);
    CHECK(state.stages[15].alpha_op == 1);
    CHECK(state.stages[14].alpha_op == 8);
    return 1;
}

static int rejects_count_capacity_and_header_malformed(void) {
    AcgcGxCanonicalTevState state;

    fill_state(&state, 1);
    state.header.active_stage_count = 0;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.active_stage_count = 17;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.stage_capacity = 15;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));

    fill_state(&state, 1);
    state.header.version = 2;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.section_id = 5;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.section_mask = UINT32_C(0x0010);
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.byte_size = 2556;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.component_valid_mask = UINT32_C(0x00000007);
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.reserved = 1;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));

    fill_state(&state, 1);
    state.header.stage_offset += 4;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.stage_record_size = 140;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.register_offset += 4;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.register_record_size = 12;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.konst_offset += 4;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.konst_record_size = 12;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.swap_table_offset += 4;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    fill_state(&state, 1);
    state.header.swap_table_record_size = 12;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));

    CHECK(!acgc_gx_canonical_tev_state_validate(NULL));
    return 1;
}

static int accepts_and_rejects_stage_domains(void) {
    AcgcGxCanonicalTevState state;
    AcgcGxCanonicalTevStage* stage;

    fill_state(&state, 1);
    stage = &state.stages[0];

    stage->tex_coord = ACGC_GX_CANONICAL_TEV_TEXCOORD_NULL;
    stage->tex_map = ACGC_GX_CANONICAL_TEV_TEXMAP_NULL;
    stage->color_chan = ACGC_GX_CANONICAL_TEV_CHANNEL_NULL;
    CHECK(acgc_gx_canonical_tev_state_validate(&state));
    stage->tex_map = ACGC_GX_CANONICAL_TEV_TEXMAP_DISABLE;
    CHECK(acgc_gx_canonical_tev_state_validate(&state));
    stage->tex_coord = 7;
    stage->tex_map = 7;
    stage->color_chan = 8;

    CHECK(expect_stage_word_rejected(&state, &stage->color_a, 16));
    CHECK(expect_stage_word_rejected(&state, &stage->alpha_a, 8));
    CHECK(expect_stage_word_rejected(&state, &stage->color_op, 2));
    CHECK(expect_stage_word_rejected(&state, &stage->alpha_op, 7));
    CHECK(expect_stage_word_rejected(&state, &stage->color_bias, 3));
    CHECK(expect_stage_word_rejected(&state, &stage->alpha_scale, 4));
    CHECK(expect_stage_word_rejected(&state, &stage->color_clamp, 2));
    CHECK(expect_stage_word_rejected(&state, &stage->color_out, 4));
    CHECK(expect_stage_word_rejected(&state, &stage->alpha_out, 4));
    CHECK(expect_stage_word_rejected(&state, &stage->tex_coord, 8));
    CHECK(expect_stage_word_rejected(&state, &stage->tex_coord, 254));
    CHECK(expect_stage_word_rejected(&state, &stage->tex_map, 8));
    CHECK(expect_stage_word_rejected(&state, &stage->tex_map, 0x101));
    CHECK(expect_stage_word_rejected(&state, &stage->color_chan, 9));
    CHECK(expect_stage_word_rejected(&state, &stage->color_chan, 254));
    CHECK(expect_stage_word_rejected(&state, &stage->k_color_sel, 8));
    CHECK(expect_stage_word_rejected(&state, &stage->k_color_sel, 11));
    CHECK(expect_stage_word_rejected(&state, &stage->k_alpha_sel, 8));
    CHECK(expect_stage_word_rejected(&state, &stage->k_alpha_sel, 15));
    CHECK(expect_stage_word_rejected(&state, &stage->ras_swap, 4));
    CHECK(expect_stage_word_rejected(&state, &stage->tex_swap, UINT32_MAX));
    CHECK(expect_stage_word_rejected(&state, &stage->ind_stage, 4));
    CHECK(expect_stage_word_rejected(&state, &stage->ind_format, 4));
    CHECK(expect_stage_word_rejected(&state, &stage->ind_bias, 8));
    CHECK(expect_stage_word_rejected(&state, &stage->ind_mtx, 4));
    CHECK(expect_stage_word_rejected(&state, &stage->ind_mtx, 8));
    CHECK(expect_stage_word_rejected(&state, &stage->ind_mtx, 12));
    CHECK(expect_stage_word_rejected(&state, &stage->ind_wrap_s, 7));
    CHECK(expect_stage_word_rejected(&state, &stage->ind_wrap_t, 7));
    CHECK(expect_stage_word_rejected(&state, &stage->ind_add_prev, 2));
    CHECK(expect_stage_word_rejected(&state, &stage->ind_lod, 2));
    CHECK(expect_stage_word_rejected(&state, &stage->ind_alpha, 4));
    CHECK(expect_stage_word_rejected(&state, &stage->color_a, UINT32_MAX));
    CHECK(expect_stage_word_rejected(&state, &stage->k_color_sel, UINT32_MAX));

    {
        static const uint32_t valid_kcolor_selectors[] = {0, 7, 12, 31};
        static const uint32_t valid_kalpha_selectors[] = {0, 7, 16, 31};
        static const uint32_t valid_indirect_matrices[] = {
            0, 1, 2, 3, 5, 6, 7, 9, 10, 11
        };
        uint32_t index;

        for (index = 0;
             index < sizeof(valid_kcolor_selectors) /
                 sizeof(valid_kcolor_selectors[0]); index++) {
            stage->k_color_sel = valid_kcolor_selectors[index];
            CHECK(acgc_gx_canonical_tev_state_validate(&state));
        }
        for (index = 0;
             index < sizeof(valid_kalpha_selectors) /
                 sizeof(valid_kalpha_selectors[0]); index++) {
            stage->k_alpha_sel = valid_kalpha_selectors[index];
            CHECK(acgc_gx_canonical_tev_state_validate(&state));
        }
        for (index = 0;
             index < sizeof(valid_indirect_matrices) /
                 sizeof(valid_indirect_matrices[0]); index++) {
            stage->ind_mtx = valid_indirect_matrices[index];
            CHECK(acgc_gx_canonical_tev_state_validate(&state));
        }
    }
    return 1;
}

static int accepts_and_rejects_register_konst_swap_bounds(void) {
    AcgcGxCanonicalTevState state;
    uint32_t index;

    fill_state(&state, 1);
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        state.registers[index].r = -1024;
        state.registers[index].g = 1023;
        state.registers[index].b = -1024;
        state.registers[index].a = 1023;
    }
    CHECK(acgc_gx_canonical_tev_state_validate(&state));
    CHECK(expect_register_value_rejected(
        &state, &state.registers[0].r, -1025));
    CHECK(expect_register_value_rejected(
        &state, &state.registers[0].g, 1024));
    CHECK(expect_register_value_rejected(
        &state, &state.registers[3].a, INT32_MIN));
    CHECK(expect_register_value_rejected(
        &state, &state.registers[3].a, INT32_MAX));

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        state.konst[index].r = 0;
        state.konst[index].g = 255;
        state.konst[index].b = 0;
        state.konst[index].a = 255;
    }
    CHECK(acgc_gx_canonical_tev_state_validate(&state));
    state.konst[0].r = 256;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    state.konst[0].r = 0;
    state.konst[1].g = UINT32_MAX;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    state.konst[1].g = 255;

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT; index++) {
        state.swap_tables[index].r = 0;
        state.swap_tables[index].g = 1;
        state.swap_tables[index].b = 2;
        state.swap_tables[index].a = 3;
    }
    CHECK(acgc_gx_canonical_tev_state_validate(&state));
    state.swap_tables[0].r = 4;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    state.swap_tables[0].r = 0;
    state.swap_tables[3].a = UINT32_MAX;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    return 1;
}

static int rejects_reserved_and_inactive_records(void) {
    AcgcGxCanonicalTevState state;

    fill_state(&state, 1);
    state.stages[0].reserved[0] = 1;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));

    fill_state(&state, 1);
    state.stages[1].color_a = 1;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));

    fill_state(&state, 2);
    state.stages[2].reserved[1] = 1;
    CHECK(!acgc_gx_canonical_tev_state_validate(&state));
    return 1;
}

static int accepts_exact_metadata_and_zero_absence(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_tev_envelope(&envelope, 1);
    entry = tev_entry(&envelope);
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(acgc_gx_canonical_tev_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(entry->section_id == ACGC_GX_CANONICAL_TEV_SECTION_ID);
    CHECK(entry->byte_size == 2560);
    CHECK(entry->count == 1);
    CHECK(entry->capacity == 16);
    CHECK(entry->valid_mask == UINT32_C(0x0020));
    CHECK(entry->reserved == 0);

    prepare_tev_envelope(&envelope, 16);
    CHECK(acgc_gx_canonical_tev_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = tev_entry(&envelope);
    CHECK(entry->section_id == ACGC_GX_CANONICAL_TEV_SECTION_ID);
    CHECK(entry->section_version == 0);
    CHECK(entry->byte_offset == 0);
    CHECK(entry->byte_size == 0);
    CHECK(entry->count == 0);
    CHECK(entry->capacity == 0);
    CHECK(entry->valid_mask == 0);
    CHECK(entry->reserved == 0);
    CHECK(acgc_gx_canonical_tev_metadata_validate(
        &envelope, sizeof(envelope)));
    return 1;
}

static int rejects_malformed_present_and_absent_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_tev_envelope(&envelope, 1);
    entry = tev_entry(&envelope);
    entry->section_version = 2;
    CHECK(!acgc_gx_canonical_tev_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_tev_envelope(&envelope, 1);
    entry = tev_entry(&envelope);
    entry->count = 0;
    CHECK(!acgc_gx_canonical_tev_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_tev_envelope(&envelope, 1);
    entry = tev_entry(&envelope);
    entry->capacity = 15;
    CHECK(!acgc_gx_canonical_tev_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_tev_envelope(&envelope, 1);
    entry = tev_entry(&envelope);
    entry->count = 17;
    entry->capacity = 17;
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(!acgc_gx_canonical_tev_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_tev_envelope(&envelope, 1);
    entry = tev_entry(&envelope);
    entry->byte_size = 2556;
    envelope.header.payload_byte_size = 2556;
    envelope.header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 2556;
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(!acgc_gx_canonical_tev_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_tev_envelope(&envelope, 1);
    entry = tev_entry(&envelope);
    entry->valid_mask = UINT32_C(0x0010);
    CHECK(!acgc_gx_canonical_tev_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = tev_entry(&envelope);
    entry->section_version = 1;
    CHECK(!acgc_gx_canonical_tev_metadata_validate(
        &envelope, sizeof(envelope)));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    envelope.header.present_state_mask =
        ACGC_GX_CANONICAL_TEV_SECTION_MASK;
    CHECK(!acgc_gx_canonical_tev_metadata_validate(
        &envelope, sizeof(envelope)));
    return 1;
}

static int encodes_wire_layout_and_preserves_output(void) {
    static const uint32_t stage_words[] = {
        15, 0, 14, 12, 7, 0, 6, 4, 0, 2, 3, 1, 3, 1, 2, 3, 1, 2,
        7, 7, 8, 31, 31, 3, 3, 3, 3, 7, 11, 6, 6, 1, 1, 3, 0, 0
    };
    AcgcGxCanonicalTevState state;
    uint8_t wire[ACGC_GX_CANONICAL_TEV_STATE_SIZE];
    uint8_t repeat[ACGC_GX_CANONICAL_TEV_STATE_SIZE];
    uint8_t before[ACGC_GX_CANONICAL_TEV_STATE_SIZE];
    uint32_t index;
    uint32_t word;

    fill_state(&state, 1);
    CHECK(acgc_gx_canonical_tev_state_encode(
        &state, wire, sizeof(wire)));

    CHECK(read_le32(wire + 0) == 1);
    CHECK(read_le32(wire + 4) == 6);
    CHECK(read_le32(wire + 8) == UINT32_C(0x20));
    CHECK(read_le32(wire + 12) == 2560);
    CHECK(read_le32(wire + 16) == 1);
    CHECK(read_le32(wire + 20) == 16);
    CHECK(read_le32(wire + 24) == UINT32_C(0xF));
    CHECK(read_le32(wire + 28) == 0);
    CHECK(read_le32(wire + 32) == 64);
    CHECK(read_le32(wire + 36) == 144);
    CHECK(read_le32(wire + 40) == 2368);
    CHECK(read_le32(wire + 44) == 16);
    CHECK(read_le32(wire + 48) == 2432);
    CHECK(read_le32(wire + 52) == 16);
    CHECK(read_le32(wire + 56) == 2496);
    CHECK(read_le32(wire + 60) == 16);

    for (word = 0; word < sizeof(stage_words) / sizeof(stage_words[0]); word++) {
        CHECK(read_le32(
            wire + ACGC_GX_CANONICAL_TEV_STAGE_OFFSET + word * 4) ==
            stage_words[word]);
    }
    for (index = 1; index < ACGC_GX_CANONICAL_TEV_STAGE_COUNT; index++) {
        for (word = 0; word < sizeof(stage_words) / sizeof(stage_words[0]);
             word++) {
            CHECK(read_le32(
                wire + ACGC_GX_CANONICAL_TEV_STAGE_OFFSET +
                    index * ACGC_GX_CANONICAL_TEV_STAGE_RECORD_SIZE +
                    word * 4) == 0);
        }
    }

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        const size_t offset =
            ACGC_GX_CANONICAL_TEV_REGISTER_OFFSET +
            index * ACGC_GX_CANONICAL_TEV_REGISTER_RECORD_SIZE;

        CHECK(read_le32(wire + offset + 0) ==
            (uint32_t)state.registers[index].r);
        CHECK(read_le32(wire + offset + 4) ==
            (uint32_t)state.registers[index].g);
        CHECK(read_le32(wire + offset + 8) ==
            (uint32_t)state.registers[index].b);
        CHECK(read_le32(wire + offset + 12) ==
            (uint32_t)state.registers[index].a);
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        const size_t offset =
            ACGC_GX_CANONICAL_TEV_KONST_OFFSET +
            index * ACGC_GX_CANONICAL_TEV_KONST_RECORD_SIZE;

        CHECK(read_le32(wire + offset + 0) == state.konst[index].r);
        CHECK(read_le32(wire + offset + 4) == state.konst[index].g);
        CHECK(read_le32(wire + offset + 8) == state.konst[index].b);
        CHECK(read_le32(wire + offset + 12) == state.konst[index].a);
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT; index++) {
        const size_t offset =
            ACGC_GX_CANONICAL_TEV_SWAP_TABLE_OFFSET +
            index * ACGC_GX_CANONICAL_TEV_SWAP_TABLE_RECORD_SIZE;

        CHECK(read_le32(wire + offset + 0) == state.swap_tables[index].r);
        CHECK(read_le32(wire + offset + 4) == state.swap_tables[index].g);
        CHECK(read_le32(wire + offset + 8) == state.swap_tables[index].b);
        CHECK(read_le32(wire + offset + 12) == state.swap_tables[index].a);
    }

    CHECK(acgc_gx_canonical_tev_state_encode(
        &state, repeat, sizeof(repeat)));
    CHECK(memcmp(wire, repeat, sizeof(wire)) == 0);

    fill_state(&state, ACGC_GX_CANONICAL_TEV_STAGE_COUNT);
    CHECK(acgc_gx_canonical_tev_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(read_le32(wire + 16) == ACGC_GX_CANONICAL_TEV_STAGE_COUNT);
    CHECK(read_le32(
        wire + ACGC_GX_CANONICAL_TEV_STAGE_OFFSET +
            15 * ACGC_GX_CANONICAL_TEV_STAGE_RECORD_SIZE + 32) == 15);
    CHECK(read_le32(
        wire + ACGC_GX_CANONICAL_TEV_STAGE_OFFSET +
            14 * ACGC_GX_CANONICAL_TEV_STAGE_RECORD_SIZE + 52) == 8);

    memset(wire, 0xA5, sizeof(wire));
    memcpy(before, wire, sizeof(before));
    CHECK(!acgc_gx_canonical_tev_state_encode(NULL, wire, sizeof(wire)));
    CHECK(memcmp(wire, before, sizeof(wire)) == 0);
    fill_state(&state, 1);
    CHECK(!acgc_gx_canonical_tev_state_encode(
        &state, wire, sizeof(wire) - 1));
    CHECK(memcmp(wire, before, sizeof(wire)) == 0);
    state.header.active_stage_count = 0;
    CHECK(!acgc_gx_canonical_tev_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(memcmp(wire, before, sizeof(wire)) == 0);
    fill_state(&state, 1);
    state.header.active_stage_count = 17;
    CHECK(!acgc_gx_canonical_tev_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(memcmp(wire, before, sizeof(wire)) == 0);
    fill_state(&state, 1);
    state.header.reserved = 1;
    CHECK(!acgc_gx_canonical_tev_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(memcmp(wire, before, sizeof(wire)) == 0);
    fill_state(&state, 1);
    state.stages[1].color_a = 1;
    CHECK(!acgc_gx_canonical_tev_state_encode(
        &state, wire, sizeof(wire)));
    CHECK(memcmp(wire, before, sizeof(wire)) == 0);
    CHECK(!acgc_gx_canonical_tev_state_encode(
        &state, NULL, sizeof(wire)));
    return 1;
}

int main(void) {
    if (!accepts_layout_boundaries_and_all_stages() ||
        !rejects_count_capacity_and_header_malformed() ||
        !accepts_and_rejects_stage_domains() ||
        !accepts_and_rejects_register_konst_swap_bounds() ||
        !rejects_reserved_and_inactive_records() ||
        !accepts_exact_metadata_and_zero_absence() ||
        !rejects_malformed_present_and_absent_metadata() ||
        !encodes_wire_layout_and_preserves_output()) {
        return 1;
    }
    puts("GX canonical TEV state tests: PASS");
    return 0;
}
