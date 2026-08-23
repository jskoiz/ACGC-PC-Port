#include "pc_gx_tev_producer.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
#include <string.h>

enum {
    PC_GX_RAW_TEV_STAGE_WORD_COUNT =
        sizeof(AcgcGxCanonicalTevStage) / sizeof(uint32_t)
};

static void pc_gx_raw_tev_fill_header(
    AcgcGxCanonicalTevHeader* header,
    uint32_t active_stage_count
) {
    memset(header, 0, sizeof(*header));
    header->version = ACGC_GX_CANONICAL_TEV_STATE_VERSION;
    header->section_id = ACGC_GX_CANONICAL_TEV_SECTION_ID;
    header->section_mask = ACGC_GX_CANONICAL_TEV_SECTION_MASK;
    header->byte_size = ACGC_GX_CANONICAL_TEV_STATE_SIZE;
    header->active_stage_count = active_stage_count;
    header->stage_capacity = ACGC_GX_CANONICAL_TEV_STAGE_CAPACITY;
    header->component_valid_mask = ACGC_GX_CANONICAL_TEV_COMPONENT_VALID_MASK;
    header->stage_offset = ACGC_GX_CANONICAL_TEV_STAGE_OFFSET;
    header->stage_record_size = ACGC_GX_CANONICAL_TEV_STAGE_RECORD_SIZE;
    header->register_offset = ACGC_GX_CANONICAL_TEV_REGISTER_OFFSET;
    header->register_record_size =
        ACGC_GX_CANONICAL_TEV_REGISTER_RECORD_SIZE;
    header->konst_offset = ACGC_GX_CANONICAL_TEV_KONST_OFFSET;
    header->konst_record_size = ACGC_GX_CANONICAL_TEV_KONST_RECORD_SIZE;
    header->swap_table_offset = ACGC_GX_CANONICAL_TEV_SWAP_TABLE_OFFSET;
    header->swap_table_record_size =
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_RECORD_SIZE;
}

static int pc_gx_raw_tev_stage_unknown_words_are_zero(
    const PCGXRawTevStage* raw
) {
    uint32_t words[PC_GX_RAW_TEV_STAGE_WORD_COUNT];
    uint32_t index;

    if (raw == NULL ||
        (raw->known_mask & ~PC_GX_RAW_TEV_STAGE_KNOWN_MASK) != 0) {
        return 0;
    }

    memcpy(words, &raw->value, sizeof(words));
    for (index = 0; index < 34; index++) {
        if ((raw->known_mask & (UINT64_C(1) << index)) == 0 &&
            words[index] != 0) {
            return 0;
        }
    }
    return words[34] == 0 && words[35] == 0;
}

static int pc_gx_raw_tev_stage_value_is_valid(
    const PCGXRawTevStage* raw
) {
    AcgcGxCanonicalTevState candidate;

    if (!pc_gx_raw_tev_stage_unknown_words_are_zero(raw)) return 0;

    /* The existing canonical validator is the single domain authority.  A
     * one-stage candidate lets it validate persistent inactive tails without
     * making those tails part of the produced active canonical state. */
    memset(&candidate, 0, sizeof(candidate));
    pc_gx_raw_tev_fill_header(&candidate.header, 1);
    candidate.stages[0] = raw->value;
    return acgc_gx_canonical_tev_state_validate(&candidate);
}

static int pc_gx_raw_tev_register_is_valid(
    const PCGXRawTevColor* raw
) {
    uint32_t index;

    if (raw == NULL || raw->known_mask != PC_GX_RAW_TEV_COMPONENT_KNOWN_MASK ||
        raw->valid != 1 || raw->reserved != 0 ||
        (raw->source != PCGX_TEV_RAW_SOURCE_COLOR_U8 &&
         raw->source != PCGX_TEV_RAW_SOURCE_COLOR_S10)) {
        return 0;
    }
    for (index = 0; index < 4; index++) {
        if (raw->source == PCGX_TEV_RAW_SOURCE_COLOR_U8 &&
            (raw->components[index] < 0 ||
             raw->components[index] > 255)) {
            return 0;
        }
        if (raw->components[index] <
                ACGC_GX_CANONICAL_TEV_REGISTER_COMPONENT_MIN ||
            raw->components[index] >
                ACGC_GX_CANONICAL_TEV_REGISTER_COMPONENT_MAX) {
            return 0;
        }
    }
    return 1;
}

static int pc_gx_raw_tev_color_is_unavailable(
    const PCGXRawTevColor* raw
) {
    uint32_t index;

    /* A zero known mask is still unavailable, not a zero value.  Accept only
     * the untouched shadow form; the dataflow check below decides whether it
     * may be represented as zero in the canonical value section. */
    if (raw == NULL || raw->known_mask != 0 || raw->valid != 0 ||
        raw->source != PCGX_TEV_RAW_SOURCE_UNAVAILABLE ||
        raw->reserved != 0) {
        return 0;
    }
    for (index = 0; index < 4; index++) {
        if (raw->components[index] != 0) return 0;
    }
    return 1;
}

static int pc_gx_raw_tev_register_is_well_formed(
    const PCGXRawTevColor* raw
) {
    return pc_gx_raw_tev_register_is_valid(raw) ||
        pc_gx_raw_tev_color_is_unavailable(raw);
}

static int pc_gx_raw_tev_konst_is_valid(
    const PCGXRawTevColor* raw
) {
    uint32_t index;

    if (raw == NULL || raw->known_mask != PC_GX_RAW_TEV_COMPONENT_KNOWN_MASK ||
        raw->valid != 1 || raw->reserved != 0 ||
        raw->source != PCGX_TEV_RAW_SOURCE_KCOLOR_U8) {
        return 0;
    }
    for (index = 0; index < 4; index++) {
        if (raw->components[index] <
                (int32_t)ACGC_GX_CANONICAL_TEV_KONST_COMPONENT_MIN ||
            raw->components[index] >
                (int32_t)ACGC_GX_CANONICAL_TEV_KONST_COMPONENT_MAX) {
            return 0;
        }
    }
    return 1;
}

static int pc_gx_raw_tev_konst_is_well_formed(
    const PCGXRawTevColor* raw
) {
    return pc_gx_raw_tev_konst_is_valid(raw) ||
        pc_gx_raw_tev_color_is_unavailable(raw);
}

static int pc_gx_raw_tev_swap_is_valid(
    const PCGXRawTevSwapTable* raw
) {
    return raw != NULL &&
        raw->known_mask == PC_GX_RAW_TEV_RECORD_KNOWN_MASK &&
        raw->value.r <= ACGC_GX_CANONICAL_TEV_SWAP_MAX &&
        raw->value.g <= ACGC_GX_CANONICAL_TEV_SWAP_MAX &&
        raw->value.b <= ACGC_GX_CANONICAL_TEV_SWAP_MAX &&
        raw->value.a <= ACGC_GX_CANONICAL_TEV_SWAP_MAX;
}

static int pc_gx_raw_tev_color_input_register(
    uint32_t input,
    uint32_t* record,
    int* alpha
) {
    if (record == NULL || alpha == NULL) return 0;
    switch (input) {
        case GX_CC_CPREV:
            *record = GX_TEVPREV;
            *alpha = 0;
            return 1;
        case GX_CC_APREV:
            *record = GX_TEVPREV;
            *alpha = 1;
            return 1;
        case GX_CC_C0:
            *record = GX_TEVREG0;
            *alpha = 0;
            return 1;
        case GX_CC_A0:
            *record = GX_TEVREG0;
            *alpha = 1;
            return 1;
        case GX_CC_C1:
            *record = GX_TEVREG1;
            *alpha = 0;
            return 1;
        case GX_CC_A1:
            *record = GX_TEVREG1;
            *alpha = 1;
            return 1;
        case GX_CC_C2:
            *record = GX_TEVREG2;
            *alpha = 0;
            return 1;
        case GX_CC_A2:
            *record = GX_TEVREG2;
            *alpha = 1;
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_raw_tev_alpha_input_register(
    uint32_t input,
    uint32_t* record
) {
    if (record == NULL) return 0;
    switch (input) {
        case GX_CA_APREV:
            *record = GX_TEVPREV;
            return 1;
        case GX_CA_A0:
            *record = GX_TEVREG0;
            return 1;
        case GX_CA_A1:
            *record = GX_TEVREG1;
            return 1;
        case GX_CA_A2:
            *record = GX_TEVREG2;
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_raw_tev_kcolor_selector_index(
    uint32_t selector,
    uint32_t* index
) {
    if (index == NULL) return 0;
    if (selector >= GX_TEV_KCSEL_K0 && selector <= GX_TEV_KCSEL_K3) {
        *index = selector - GX_TEV_KCSEL_K0;
        return 1;
    }
    if (selector >= GX_TEV_KCSEL_K0_R &&
        selector <= GX_TEV_KCSEL_K3_A) {
        *index = (selector - GX_TEV_KCSEL_K0_R) %
            ACGC_GX_CANONICAL_TEV_KONST_COUNT;
        return 1;
    }
    return 0;
}

static int pc_gx_raw_tev_kalpha_selector_index(
    uint32_t selector,
    uint32_t* index
) {
    if (index == NULL || selector < GX_TEV_KASEL_K0_R ||
        selector > GX_TEV_KASEL_K3_A) {
        return 0;
    }
    *index = (selector - GX_TEV_KASEL_K0_R) %
        ACGC_GX_CANONICAL_TEV_KONST_COUNT;
    return 1;
}

static int pc_gx_raw_tev_stage_reads_unavailable(
    const PCGXRawTevIndirect* input,
    const PCGXRawTevStage* stage,
    const uint8_t* color_defined,
    const uint8_t* alpha_defined
) {
    const uint32_t color_inputs[4] = {
        stage->value.color_a,
        stage->value.color_b,
        stage->value.color_c,
        stage->value.color_d
    };
    const uint32_t alpha_inputs[4] = {
        stage->value.alpha_a,
        stage->value.alpha_b,
        stage->value.alpha_c,
        stage->value.alpha_d
    };
    uint32_t input_index;
    uint32_t record_index;
    int alpha;

    for (input_index = 0; input_index < 4; input_index++) {
        if (pc_gx_raw_tev_color_input_register(
                color_inputs[input_index], &record_index, &alpha) &&
            !((alpha ? alpha_defined : color_defined)[record_index]) &&
            input->registers[record_index].known_mask == 0) {
            return 1;
        }
    }
    for (input_index = 0; input_index < 4; input_index++) {
        if (pc_gx_raw_tev_alpha_input_register(
                alpha_inputs[input_index], &record_index) &&
            !alpha_defined[record_index] &&
            input->registers[record_index].known_mask == 0) {
            return 1;
        }
    }

    if (stage->value.color_a == GX_CC_KONST ||
        stage->value.color_b == GX_CC_KONST ||
        stage->value.color_c == GX_CC_KONST ||
        stage->value.color_d == GX_CC_KONST) {
        if (pc_gx_raw_tev_kcolor_selector_index(
                stage->value.k_color_sel, &record_index) &&
            input->konst[record_index].known_mask == 0) {
            return 1;
        }
    }
    if (stage->value.alpha_a == GX_CA_KONST ||
        stage->value.alpha_b == GX_CA_KONST ||
        stage->value.alpha_c == GX_CA_KONST ||
        stage->value.alpha_d == GX_CA_KONST) {
        if (pc_gx_raw_tev_kalpha_selector_index(
                stage->value.k_alpha_sel, &record_index) &&
            input->konst[record_index].known_mask == 0) {
            return 1;
        }
    }
    return 0;
}

static int pc_gx_raw_tev_dataflow_is_valid(
    const PCGXRawTevIndirect* input
) {
    uint8_t color_defined[ACGC_GX_CANONICAL_TEV_REGISTER_COUNT] = {0};
    uint8_t alpha_defined[ACGC_GX_CANONICAL_TEV_REGISTER_COUNT] = {0};
    uint32_t index;

    /* Each active stage reads its inputs before defining its color and alpha
     * output registers.  Keep those definitions independent so an unknown
     * initial color channel cannot satisfy an alpha read, or vice versa. */
    for (index = 0; index < input->active_tev_stage_count; index++) {
        const PCGXRawTevStage* stage = &input->stages[index];

        if (pc_gx_raw_tev_stage_reads_unavailable(
                input, stage, color_defined, alpha_defined)) {
            return 0;
        }
        color_defined[stage->value.color_out] = 1;
        alpha_defined[stage->value.alpha_out] = 1;
    }
    return 1;
}

static int pc_gx_raw_tev_input_is_valid(
    const PCGXRawTevIndirect* input
) {
    uint32_t index;

    if (input == NULL || input->invalid != 0 ||
        input->active_tev_stage_count_known != 1 ||
        input->active_tev_stage_count <
            ACGC_GX_CANONICAL_TEV_ACTIVE_STAGE_COUNT_MIN ||
        input->active_tev_stage_count >
            ACGC_GX_CANONICAL_TEV_ACTIVE_STAGE_COUNT_MAX) {
        return 0;
    }

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_STAGE_COUNT; index++) {
        const PCGXRawTevStage* stage = &input->stages[index];

        if (!pc_gx_raw_tev_stage_value_is_valid(stage)) return 0;
        if (index < input->active_tev_stage_count &&
            stage->known_mask != PC_GX_RAW_TEV_STAGE_KNOWN_MASK) {
            return 0;
        }
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        if (!pc_gx_raw_tev_register_is_well_formed(&input->registers[index])) {
            return 0;
        }
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        if (!pc_gx_raw_tev_konst_is_well_formed(&input->konst[index])) {
            return 0;
        }
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT; index++) {
        if (!pc_gx_raw_tev_swap_is_valid(&input->swap_tables[index])) {
            return 0;
        }
    }
    return pc_gx_raw_tev_dataflow_is_valid(input);
}

int pc_gx_raw_tev_build_canonical(
    const PCGXRawTevIndirect* input,
    AcgcGxCanonicalTevState* output
) {
    AcgcGxCanonicalTevState candidate;
    uint32_t index;

    if (output == NULL || !pc_gx_raw_tev_input_is_valid(input)) return 0;

    memset(&candidate, 0, sizeof(candidate));
    pc_gx_raw_tev_fill_header(
        &candidate.header,
        input->active_tev_stage_count
    );

    for (index = 0; index < input->active_tev_stage_count; index++) {
        candidate.stages[index] = input->stages[index].value;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        /* Unknown records are zero only after dataflow proves their initial
         * values cannot be observed. */
        if (input->registers[index].known_mask != 0) {
            candidate.registers[index].r =
                input->registers[index].components[0];
            candidate.registers[index].g =
                input->registers[index].components[1];
            candidate.registers[index].b =
                input->registers[index].components[2];
            candidate.registers[index].a =
                input->registers[index].components[3];
        }
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        if (input->konst[index].known_mask != 0) {
            candidate.konst[index].r =
                (uint32_t)input->konst[index].components[0];
            candidate.konst[index].g =
                (uint32_t)input->konst[index].components[1];
            candidate.konst[index].b =
                (uint32_t)input->konst[index].components[2];
            candidate.konst[index].a =
                (uint32_t)input->konst[index].components[3];
        }
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT; index++) {
        candidate.swap_tables[index] = input->swap_tables[index].value;
    }

    if (!acgc_gx_canonical_tev_state_validate(&candidate)) return 0;

    *output = candidate;
    return 1;
}
