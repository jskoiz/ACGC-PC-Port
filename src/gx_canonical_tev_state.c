#include "acgc/gx_canonical_tev_state.h"

#include <string.h>

static int canonical_tev_word_is_bounded(
    uint32_t value,
    uint32_t minimum,
    uint32_t maximum
) {
    return value >= minimum && value <= maximum;
}

static int canonical_tev_operation_is_valid(uint32_t operation) {
    return operation == ACGC_GX_CANONICAL_TEV_OPERATION_ADD ||
        operation == ACGC_GX_CANONICAL_TEV_OPERATION_SUB ||
        (operation >= ACGC_GX_CANONICAL_TEV_OPERATION_COMPARE_MIN &&
         operation <= ACGC_GX_CANONICAL_TEV_OPERATION_COMPARE_MAX);
}

static int canonical_tev_tex_coord_is_valid(uint32_t tex_coord) {
    return canonical_tev_word_is_bounded(
            tex_coord,
            ACGC_GX_CANONICAL_TEV_TEXCOORD_MIN,
            ACGC_GX_CANONICAL_TEV_TEXCOORD_MAX) ||
        tex_coord == ACGC_GX_CANONICAL_TEV_TEXCOORD_NULL;
}

static int canonical_tev_tex_map_is_valid(uint32_t tex_map) {
    return canonical_tev_word_is_bounded(
            tex_map,
            ACGC_GX_CANONICAL_TEV_TEXMAP_MIN,
            ACGC_GX_CANONICAL_TEV_TEXMAP_MAX) ||
        tex_map == ACGC_GX_CANONICAL_TEV_TEXMAP_NULL ||
        tex_map == ACGC_GX_CANONICAL_TEV_TEXMAP_DISABLE;
}

static int canonical_tev_channel_is_valid(uint32_t color_chan) {
    return canonical_tev_word_is_bounded(
            color_chan,
            ACGC_GX_CANONICAL_TEV_CHANNEL_MIN,
            ACGC_GX_CANONICAL_TEV_CHANNEL_MAX) ||
        color_chan == ACGC_GX_CANONICAL_TEV_CHANNEL_NULL;
}

static int canonical_tev_kcolor_selector_is_valid(uint32_t selector) {
    return canonical_tev_word_is_bounded(
            selector,
            ACGC_GX_CANONICAL_TEV_KCOLOR_SELECTOR_MIN,
            ACGC_GX_CANONICAL_TEV_KCOLOR_SELECTOR_LOW_MAX) ||
        canonical_tev_word_is_bounded(
            selector,
            ACGC_GX_CANONICAL_TEV_KCOLOR_SELECTOR_HIGH_MIN,
            ACGC_GX_CANONICAL_TEV_KCOLOR_SELECTOR_MAX);
}

static int canonical_tev_kalpha_selector_is_valid(uint32_t selector) {
    return canonical_tev_word_is_bounded(
            selector,
            ACGC_GX_CANONICAL_TEV_KALPHA_SELECTOR_MIN,
            ACGC_GX_CANONICAL_TEV_KALPHA_SELECTOR_LOW_MAX) ||
        canonical_tev_word_is_bounded(
            selector,
            ACGC_GX_CANONICAL_TEV_KALPHA_SELECTOR_HIGH_MIN,
            ACGC_GX_CANONICAL_TEV_KALPHA_SELECTOR_MAX);
}

static int canonical_tev_indirect_matrix_is_valid(uint32_t matrix) {
    switch (matrix) {
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_OFF:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_0:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_1:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_2:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S0:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S1:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S2:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T0:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T1:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T2:
            return 1;
        default:
            return 0;
    }
}

static int canonical_tev_stage_validate(
    const AcgcGxCanonicalTevStage* stage
) {
    return stage != NULL &&
        canonical_tev_word_is_bounded(
            stage->color_a,
            ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MIN,
            ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX) &&
        canonical_tev_word_is_bounded(
            stage->color_b,
            ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MIN,
            ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX) &&
        canonical_tev_word_is_bounded(
            stage->color_c,
            ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MIN,
            ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX) &&
        canonical_tev_word_is_bounded(
            stage->color_d,
            ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MIN,
            ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX) &&
        canonical_tev_word_is_bounded(
            stage->alpha_a,
            ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MIN,
            ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX) &&
        canonical_tev_word_is_bounded(
            stage->alpha_b,
            ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MIN,
            ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX) &&
        canonical_tev_word_is_bounded(
            stage->alpha_c,
            ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MIN,
            ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX) &&
        canonical_tev_word_is_bounded(
            stage->alpha_d,
            ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MIN,
            ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX) &&
        canonical_tev_operation_is_valid(stage->color_op) &&
        canonical_tev_word_is_bounded(
            stage->color_bias,
            ACGC_GX_CANONICAL_TEV_BIAS_MIN,
            ACGC_GX_CANONICAL_TEV_BIAS_MAX) &&
        canonical_tev_word_is_bounded(
            stage->color_scale,
            ACGC_GX_CANONICAL_TEV_SCALE_MIN,
            ACGC_GX_CANONICAL_TEV_SCALE_MAX) &&
        canonical_tev_word_is_bounded(
            stage->color_clamp,
            ACGC_GX_CANONICAL_TEV_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX) &&
        canonical_tev_word_is_bounded(
            stage->color_out,
            ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MIN,
            ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MAX) &&
        canonical_tev_operation_is_valid(stage->alpha_op) &&
        canonical_tev_word_is_bounded(
            stage->alpha_bias,
            ACGC_GX_CANONICAL_TEV_BIAS_MIN,
            ACGC_GX_CANONICAL_TEV_BIAS_MAX) &&
        canonical_tev_word_is_bounded(
            stage->alpha_scale,
            ACGC_GX_CANONICAL_TEV_SCALE_MIN,
            ACGC_GX_CANONICAL_TEV_SCALE_MAX) &&
        canonical_tev_word_is_bounded(
            stage->alpha_clamp,
            ACGC_GX_CANONICAL_TEV_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX) &&
        canonical_tev_word_is_bounded(
            stage->alpha_out,
            ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MIN,
            ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MAX) &&
        canonical_tev_tex_coord_is_valid(stage->tex_coord) &&
        canonical_tev_tex_map_is_valid(stage->tex_map) &&
        canonical_tev_channel_is_valid(stage->color_chan) &&
        canonical_tev_kcolor_selector_is_valid(stage->k_color_sel) &&
        canonical_tev_kalpha_selector_is_valid(stage->k_alpha_sel) &&
        canonical_tev_word_is_bounded(
            stage->ras_swap,
            ACGC_GX_CANONICAL_TEV_SWAP_MIN,
            ACGC_GX_CANONICAL_TEV_SWAP_MAX) &&
        canonical_tev_word_is_bounded(
            stage->tex_swap,
            ACGC_GX_CANONICAL_TEV_SWAP_MIN,
            ACGC_GX_CANONICAL_TEV_SWAP_MAX) &&
        canonical_tev_word_is_bounded(
            stage->ind_stage,
            ACGC_GX_CANONICAL_TEV_INDIRECT_STAGE_MIN,
            ACGC_GX_CANONICAL_TEV_INDIRECT_STAGE_MAX) &&
        canonical_tev_word_is_bounded(
            stage->ind_format,
            ACGC_GX_CANONICAL_TEV_INDIRECT_FORMAT_MIN,
            ACGC_GX_CANONICAL_TEV_INDIRECT_FORMAT_MAX) &&
        canonical_tev_word_is_bounded(
            stage->ind_bias,
            ACGC_GX_CANONICAL_TEV_INDIRECT_BIAS_MIN,
            ACGC_GX_CANONICAL_TEV_INDIRECT_BIAS_MAX) &&
        canonical_tev_indirect_matrix_is_valid(stage->ind_mtx) &&
        canonical_tev_word_is_bounded(
            stage->ind_wrap_s,
            ACGC_GX_CANONICAL_TEV_INDIRECT_WRAP_MIN,
            ACGC_GX_CANONICAL_TEV_INDIRECT_WRAP_MAX) &&
        canonical_tev_word_is_bounded(
            stage->ind_wrap_t,
            ACGC_GX_CANONICAL_TEV_INDIRECT_WRAP_MIN,
            ACGC_GX_CANONICAL_TEV_INDIRECT_WRAP_MAX) &&
        canonical_tev_word_is_bounded(
            stage->ind_add_prev,
            ACGC_GX_CANONICAL_TEV_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX) &&
        canonical_tev_word_is_bounded(
            stage->ind_lod,
            ACGC_GX_CANONICAL_TEV_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX) &&
        canonical_tev_word_is_bounded(
            stage->ind_alpha,
            ACGC_GX_CANONICAL_TEV_INDIRECT_ALPHA_MIN,
            ACGC_GX_CANONICAL_TEV_INDIRECT_ALPHA_MAX) &&
        stage->reserved[0] == 0 && stage->reserved[1] == 0;
}

static int canonical_tev_register_validate(
    const AcgcGxCanonicalTevRegister* record
) {
    return record != NULL &&
        record->r >= ACGC_GX_CANONICAL_TEV_REGISTER_COMPONENT_MIN &&
        record->r <= ACGC_GX_CANONICAL_TEV_REGISTER_COMPONENT_MAX &&
        record->g >= ACGC_GX_CANONICAL_TEV_REGISTER_COMPONENT_MIN &&
        record->g <= ACGC_GX_CANONICAL_TEV_REGISTER_COMPONENT_MAX &&
        record->b >= ACGC_GX_CANONICAL_TEV_REGISTER_COMPONENT_MIN &&
        record->b <= ACGC_GX_CANONICAL_TEV_REGISTER_COMPONENT_MAX &&
        record->a >= ACGC_GX_CANONICAL_TEV_REGISTER_COMPONENT_MIN &&
        record->a <= ACGC_GX_CANONICAL_TEV_REGISTER_COMPONENT_MAX;
}

static int canonical_tev_konst_validate(
    const AcgcGxCanonicalTevKonst* record
) {
    return record != NULL &&
        canonical_tev_word_is_bounded(
            record->r,
            ACGC_GX_CANONICAL_TEV_KONST_COMPONENT_MIN,
            ACGC_GX_CANONICAL_TEV_KONST_COMPONENT_MAX) &&
        canonical_tev_word_is_bounded(
            record->g,
            ACGC_GX_CANONICAL_TEV_KONST_COMPONENT_MIN,
            ACGC_GX_CANONICAL_TEV_KONST_COMPONENT_MAX) &&
        canonical_tev_word_is_bounded(
            record->b,
            ACGC_GX_CANONICAL_TEV_KONST_COMPONENT_MIN,
            ACGC_GX_CANONICAL_TEV_KONST_COMPONENT_MAX) &&
        canonical_tev_word_is_bounded(
            record->a,
            ACGC_GX_CANONICAL_TEV_KONST_COMPONENT_MIN,
            ACGC_GX_CANONICAL_TEV_KONST_COMPONENT_MAX);
}

static int canonical_tev_swap_table_validate(
    const AcgcGxCanonicalTevSwapTable* table
) {
    return table != NULL &&
        canonical_tev_word_is_bounded(
            table->r,
            ACGC_GX_CANONICAL_TEV_SWAP_MIN,
            ACGC_GX_CANONICAL_TEV_SWAP_MAX) &&
        canonical_tev_word_is_bounded(
            table->g,
            ACGC_GX_CANONICAL_TEV_SWAP_MIN,
            ACGC_GX_CANONICAL_TEV_SWAP_MAX) &&
        canonical_tev_word_is_bounded(
            table->b,
            ACGC_GX_CANONICAL_TEV_SWAP_MIN,
            ACGC_GX_CANONICAL_TEV_SWAP_MAX) &&
        canonical_tev_word_is_bounded(
            table->a,
            ACGC_GX_CANONICAL_TEV_SWAP_MIN,
            ACGC_GX_CANONICAL_TEV_SWAP_MAX);
}

static int canonical_tev_header_validate(
    const AcgcGxCanonicalTevHeader* header
) {
    return header != NULL &&
        header->version == ACGC_GX_CANONICAL_TEV_STATE_VERSION &&
        header->section_id == ACGC_GX_CANONICAL_TEV_SECTION_ID &&
        header->section_mask == ACGC_GX_CANONICAL_TEV_SECTION_MASK &&
        header->byte_size == ACGC_GX_CANONICAL_TEV_STATE_SIZE &&
        header->active_stage_count >=
            ACGC_GX_CANONICAL_TEV_ACTIVE_STAGE_COUNT_MIN &&
        header->active_stage_count <=
            ACGC_GX_CANONICAL_TEV_ACTIVE_STAGE_COUNT_MAX &&
        header->stage_capacity == ACGC_GX_CANONICAL_TEV_STAGE_CAPACITY &&
        header->component_valid_mask ==
            ACGC_GX_CANONICAL_TEV_COMPONENT_VALID_MASK &&
        header->reserved == 0 &&
        header->stage_offset == ACGC_GX_CANONICAL_TEV_STAGE_OFFSET &&
        header->stage_record_size ==
            ACGC_GX_CANONICAL_TEV_STAGE_RECORD_SIZE &&
        header->register_offset == ACGC_GX_CANONICAL_TEV_REGISTER_OFFSET &&
        header->register_record_size ==
            ACGC_GX_CANONICAL_TEV_REGISTER_RECORD_SIZE &&
        header->konst_offset == ACGC_GX_CANONICAL_TEV_KONST_OFFSET &&
        header->konst_record_size ==
            ACGC_GX_CANONICAL_TEV_KONST_RECORD_SIZE &&
        header->swap_table_offset ==
            ACGC_GX_CANONICAL_TEV_SWAP_TABLE_OFFSET &&
        header->swap_table_record_size ==
            ACGC_GX_CANONICAL_TEV_SWAP_TABLE_RECORD_SIZE;
}

int acgc_gx_canonical_tev_state_validate(
    const AcgcGxCanonicalTevState* state
) {
    static const AcgcGxCanonicalTevStage zero_stage = {0};
    uint32_t index;

    if (state == NULL || !canonical_tev_header_validate(&state->header)) {
        return 0;
    }

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_STAGE_COUNT; index++) {
        if (index < state->header.active_stage_count) {
            if (!canonical_tev_stage_validate(&state->stages[index])) {
                return 0;
            }
        } else if (memcmp(&state->stages[index], &zero_stage,
                          sizeof(zero_stage)) != 0) {
            return 0;
        }
    }

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        if (!canonical_tev_register_validate(&state->registers[index])) {
            return 0;
        }
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        if (!canonical_tev_konst_validate(&state->konst[index])) {
            return 0;
        }
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT; index++) {
        if (!canonical_tev_swap_table_validate(&state->swap_tables[index])) {
            return 0;
        }
    }
    return 1;
}

static int canonical_tev_entry_is_absent(
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry
) {
    return entry != NULL &&
        entry->section_id == ACGC_GX_CANONICAL_TEV_SECTION_ID &&
        entry->section_version == 0 &&
        entry->byte_offset == 0 &&
        entry->byte_size == 0 &&
        entry->count == 0 &&
        entry->capacity == 0 &&
        entry->valid_mask == 0 &&
        entry->reserved == 0;
}

int acgc_gx_canonical_tev_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
) {
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    if (!acgc_gx_canonical_envelope_validate(
            envelope, envelope_byte_size)) {
        return 0;
    }

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_TEV_SECTION_ID - 1];
    if ((envelope->header.present_state_mask &
            ACGC_GX_CANONICAL_TEV_SECTION_MASK) == 0) {
        return canonical_tev_entry_is_absent(entry);
    }

    return entry->section_id == ACGC_GX_CANONICAL_TEV_SECTION_ID &&
        entry->section_version == ACGC_GX_CANONICAL_TEV_SECTION_VERSION &&
        entry->byte_size == ACGC_GX_CANONICAL_TEV_SECTION_BYTE_SIZE &&
        entry->count >= ACGC_GX_CANONICAL_TEV_SECTION_COUNT_MIN &&
        entry->count <= ACGC_GX_CANONICAL_TEV_SECTION_COUNT_MAX &&
        entry->capacity == ACGC_GX_CANONICAL_TEV_SECTION_CAPACITY &&
        entry->valid_mask == ACGC_GX_CANONICAL_TEV_SECTION_MASK &&
        entry->reserved == 0;
}

static void canonical_tev_write_le32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)(value & UINT32_C(0xFF));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    destination[3] = (uint8_t)((value >> 24) & UINT32_C(0xFF));
}

static void canonical_tev_encode_word(
    uint8_t* destination,
    size_t* offset,
    uint32_t value
) {
    canonical_tev_write_le32(destination + *offset, value);
    *offset += sizeof(uint32_t);
}

int acgc_gx_canonical_tev_state_encode(
    const AcgcGxCanonicalTevState* state,
    uint8_t* destination,
    size_t destination_byte_size
) {
    uint8_t encoded[ACGC_GX_CANONICAL_TEV_STATE_SIZE];
    size_t offset = 0;
    uint32_t index;

    if (state == NULL || destination == NULL ||
        destination_byte_size != ACGC_GX_CANONICAL_TEV_STATE_SIZE ||
        !acgc_gx_canonical_tev_state_validate(state)) {
        return 0;
    }

    memset(encoded, 0, sizeof(encoded));
    canonical_tev_encode_word(encoded, &offset, state->header.version);
    canonical_tev_encode_word(encoded, &offset, state->header.section_id);
    canonical_tev_encode_word(encoded, &offset, state->header.section_mask);
    canonical_tev_encode_word(encoded, &offset, state->header.byte_size);
    canonical_tev_encode_word(
        encoded, &offset, state->header.active_stage_count);
    canonical_tev_encode_word(encoded, &offset, state->header.stage_capacity);
    canonical_tev_encode_word(
        encoded, &offset, state->header.component_valid_mask);
    canonical_tev_encode_word(encoded, &offset, state->header.reserved);
    canonical_tev_encode_word(encoded, &offset, state->header.stage_offset);
    canonical_tev_encode_word(
        encoded, &offset, state->header.stage_record_size);
    canonical_tev_encode_word(encoded, &offset, state->header.register_offset);
    canonical_tev_encode_word(
        encoded, &offset, state->header.register_record_size);
    canonical_tev_encode_word(encoded, &offset, state->header.konst_offset);
    canonical_tev_encode_word(
        encoded, &offset, state->header.konst_record_size);
    canonical_tev_encode_word(
        encoded, &offset, state->header.swap_table_offset);
    canonical_tev_encode_word(
        encoded, &offset, state->header.swap_table_record_size);

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_STAGE_COUNT; index++) {
        const AcgcGxCanonicalTevStage* stage = &state->stages[index];

        canonical_tev_encode_word(encoded, &offset, stage->color_a);
        canonical_tev_encode_word(encoded, &offset, stage->color_b);
        canonical_tev_encode_word(encoded, &offset, stage->color_c);
        canonical_tev_encode_word(encoded, &offset, stage->color_d);
        canonical_tev_encode_word(encoded, &offset, stage->alpha_a);
        canonical_tev_encode_word(encoded, &offset, stage->alpha_b);
        canonical_tev_encode_word(encoded, &offset, stage->alpha_c);
        canonical_tev_encode_word(encoded, &offset, stage->alpha_d);
        canonical_tev_encode_word(encoded, &offset, stage->color_op);
        canonical_tev_encode_word(encoded, &offset, stage->color_bias);
        canonical_tev_encode_word(encoded, &offset, stage->color_scale);
        canonical_tev_encode_word(encoded, &offset, stage->color_clamp);
        canonical_tev_encode_word(encoded, &offset, stage->color_out);
        canonical_tev_encode_word(encoded, &offset, stage->alpha_op);
        canonical_tev_encode_word(encoded, &offset, stage->alpha_bias);
        canonical_tev_encode_word(encoded, &offset, stage->alpha_scale);
        canonical_tev_encode_word(encoded, &offset, stage->alpha_clamp);
        canonical_tev_encode_word(encoded, &offset, stage->alpha_out);
        canonical_tev_encode_word(encoded, &offset, stage->tex_coord);
        canonical_tev_encode_word(encoded, &offset, stage->tex_map);
        canonical_tev_encode_word(encoded, &offset, stage->color_chan);
        canonical_tev_encode_word(encoded, &offset, stage->k_color_sel);
        canonical_tev_encode_word(encoded, &offset, stage->k_alpha_sel);
        canonical_tev_encode_word(encoded, &offset, stage->ras_swap);
        canonical_tev_encode_word(encoded, &offset, stage->tex_swap);
        canonical_tev_encode_word(encoded, &offset, stage->ind_stage);
        canonical_tev_encode_word(encoded, &offset, stage->ind_format);
        canonical_tev_encode_word(encoded, &offset, stage->ind_bias);
        canonical_tev_encode_word(encoded, &offset, stage->ind_mtx);
        canonical_tev_encode_word(encoded, &offset, stage->ind_wrap_s);
        canonical_tev_encode_word(encoded, &offset, stage->ind_wrap_t);
        canonical_tev_encode_word(encoded, &offset, stage->ind_add_prev);
        canonical_tev_encode_word(encoded, &offset, stage->ind_lod);
        canonical_tev_encode_word(encoded, &offset, stage->ind_alpha);
        canonical_tev_encode_word(encoded, &offset, stage->reserved[0]);
        canonical_tev_encode_word(encoded, &offset, stage->reserved[1]);
    }

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        const AcgcGxCanonicalTevRegister* record = &state->registers[index];

        canonical_tev_encode_word(encoded, &offset, (uint32_t)record->r);
        canonical_tev_encode_word(encoded, &offset, (uint32_t)record->g);
        canonical_tev_encode_word(encoded, &offset, (uint32_t)record->b);
        canonical_tev_encode_word(encoded, &offset, (uint32_t)record->a);
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        const AcgcGxCanonicalTevKonst* record = &state->konst[index];

        canonical_tev_encode_word(encoded, &offset, record->r);
        canonical_tev_encode_word(encoded, &offset, record->g);
        canonical_tev_encode_word(encoded, &offset, record->b);
        canonical_tev_encode_word(encoded, &offset, record->a);
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT; index++) {
        const AcgcGxCanonicalTevSwapTable* record = &state->swap_tables[index];

        canonical_tev_encode_word(encoded, &offset, record->r);
        canonical_tev_encode_word(encoded, &offset, record->g);
        canonical_tev_encode_word(encoded, &offset, record->b);
        canonical_tev_encode_word(encoded, &offset, record->a);
    }

    if (offset != ACGC_GX_CANONICAL_TEV_STATE_SIZE) {
        return 0;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_STATE_SIZE; index++) {
        destination[index] = encoded[index];
    }
    return 1;
}
