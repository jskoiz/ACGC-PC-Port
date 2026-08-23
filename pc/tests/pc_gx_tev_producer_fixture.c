#include "pc_gx_tev_producer.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 0; \
    } \
} while (0)

static void fill_stage(
    PCGXRawTevStage* stage,
    uint32_t index
) {
    memset(stage, 0, sizeof(*stage));
    stage->value.color_a = 15;
    stage->value.color_b = index % 16;
    stage->value.color_c = 14;
    stage->value.color_d = 12;
    stage->value.alpha_a = 7;
    stage->value.alpha_b = index % 8;
    stage->value.alpha_c = 6;
    stage->value.alpha_d = 4;
    stage->value.color_op = ACGC_GX_CANONICAL_TEV_OPERATION_ADD;
    stage->value.color_bias = 2;
    stage->value.color_scale = 3;
    stage->value.color_clamp = 1;
    stage->value.color_out = 3;
    stage->value.alpha_op = ACGC_GX_CANONICAL_TEV_OPERATION_SUB;
    stage->value.alpha_bias = 2;
    stage->value.alpha_scale = 3;
    stage->value.alpha_clamp = 1;
    stage->value.alpha_out = 2;
    stage->value.tex_coord = 7;
    stage->value.tex_map = 7;
    stage->value.color_chan = 8;
    stage->value.k_color_sel = 31;
    stage->value.k_alpha_sel = 31;
    stage->value.ras_swap = 3;
    stage->value.tex_swap = 3;
    stage->value.ind_stage = 3;
    stage->value.ind_format = 3;
    stage->value.ind_bias = 7;
    stage->value.ind_mtx = ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T2;
    stage->value.ind_wrap_s = 6;
    stage->value.ind_wrap_t = 6;
    stage->value.ind_add_prev = 1;
    stage->value.ind_lod = 1;
    stage->value.ind_alpha = 3;
    stage->known_mask = PC_GX_RAW_TEV_STAGE_KNOWN_MASK;
}

static void set_stage_zero_replace(PCGXRawTevStage* stage) {
    stage->value.color_a = GX_CC_ZERO;
    stage->value.color_b = GX_CC_ZERO;
    stage->value.color_c = GX_CC_ZERO;
    stage->value.color_d = GX_CC_TEXC;
    stage->value.alpha_a = GX_CA_ZERO;
    stage->value.alpha_b = GX_CA_ZERO;
    stage->value.alpha_c = GX_CA_ZERO;
    stage->value.alpha_d = GX_CA_TEXA;
}

static void set_stage_gx_replace(PCGXRawTevStage* stage) {
    set_stage_zero_replace(stage);
    stage->value.color_op = GX_TEV_ADD;
    stage->value.color_bias = GX_TB_ZERO;
    stage->value.color_scale = GX_CS_SCALE_1;
    stage->value.color_clamp = GX_TRUE;
    stage->value.color_out = GX_TEVPREV;
    stage->value.alpha_op = GX_TEV_ADD;
    stage->value.alpha_bias = GX_TB_ZERO;
    stage->value.alpha_scale = GX_CS_SCALE_1;
    stage->value.alpha_clamp = GX_TRUE;
    stage->value.alpha_out = GX_TEVPREV;
    stage->value.tex_coord = GX_TEXCOORD0;
    stage->value.tex_map = GX_TEXMAP0;
    stage->value.color_chan = GX_COLOR0A0;
    stage->value.k_color_sel = GX_TEV_KCSEL_1_4;
    stage->value.k_alpha_sel = GX_TEV_KASEL_1;
    stage->value.ras_swap = GX_TEV_SWAP0;
    stage->value.tex_swap = GX_TEV_SWAP0;
    stage->value.ind_stage = 0;
    stage->value.ind_format = 0;
    stage->value.ind_bias = 0;
    stage->value.ind_mtx = 0;
    stage->value.ind_wrap_s = 0;
    stage->value.ind_wrap_t = 0;
    stage->value.ind_add_prev = 0;
    stage->value.ind_lod = 0;
    stage->value.ind_alpha = 0;
    stage->known_mask = PC_GX_RAW_TEV_STAGE_KNOWN_MASK;
}

static void set_stage_one_blend(PCGXRawTevStage* stage) {
    stage->value.color_a = GX_CC_CPREV;
    stage->value.color_b = GX_CC_ONE;
    stage->value.color_c = GX_CC_TEXC;
    stage->value.color_d = GX_CC_ZERO;
    stage->value.alpha_a = GX_CA_ZERO;
    stage->value.alpha_b = GX_CA_TEXA;
    stage->value.alpha_c = GX_CA_APREV;
    stage->value.alpha_d = GX_CA_ZERO;
    stage->value.color_op = GX_TEV_ADD;
    stage->value.color_bias = GX_TB_ZERO;
    stage->value.color_scale = GX_CS_SCALE_1;
    stage->value.color_clamp = GX_TRUE;
    stage->value.color_out = GX_TEVPREV;
    stage->value.alpha_op = GX_TEV_ADD;
    stage->value.alpha_bias = GX_TB_ZERO;
    stage->value.alpha_scale = GX_CS_SCALE_1;
    stage->value.alpha_clamp = GX_TRUE;
    stage->value.alpha_out = GX_TEVPREV;
    stage->known_mask = PC_GX_RAW_TEV_STAGE_KNOWN_MASK;
}

static void set_valid_colors(PCGXRawTevIndirect* raw) {
    static const int32_t register_values[4][4] = {
        {0, 1, 2, 3},
        {-1024, 0, 1023, -1},
        {10, 20, 30, 40},
        {-1, -2, -3, -4}
    };
    uint32_t index;

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        memcpy(raw->registers[index].components, register_values[index],
               sizeof(register_values[index]));
        raw->registers[index].valid = 1;
        raw->registers[index].source = index & 1 ?
            PCGX_TEV_RAW_SOURCE_COLOR_S10 : PCGX_TEV_RAW_SOURCE_COLOR_U8;
        raw->registers[index].known_mask =
            PC_GX_RAW_TEV_COMPONENT_KNOWN_MASK;
        raw->registers[index].reserved = 0;
    }

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        raw->konst[index].components[0] = (int32_t)index;
        raw->konst[index].components[1] = 255 - (int32_t)index;
        raw->konst[index].components[2] = 10 + (int32_t)index;
        raw->konst[index].components[3] = 240 - (int32_t)index;
        raw->konst[index].valid = 1;
        raw->konst[index].source = PCGX_TEV_RAW_SOURCE_KCOLOR_U8;
        raw->konst[index].known_mask =
            PC_GX_RAW_TEV_COMPONENT_KNOWN_MASK;
        raw->konst[index].reserved = 0;
    }
}

static void set_unavailable_color(PCGXRawTevColor* color) {
    memset(color, 0, sizeof(*color));
}

static void set_all_colors_unavailable(PCGXRawTevIndirect* raw) {
    uint32_t index;

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        set_unavailable_color(&raw->registers[index]);
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        set_unavailable_color(&raw->konst[index]);
    }
}

static void set_valid_swaps(PCGXRawTevIndirect* raw) {
    uint32_t index;

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT; index++) {
        raw->swap_tables[index].value.r = index & 3;
        raw->swap_tables[index].value.g = (index + 1) & 3;
        raw->swap_tables[index].value.b = (index + 2) & 3;
        raw->swap_tables[index].value.a = (index + 3) & 3;
        raw->swap_tables[index].known_mask =
            PC_GX_RAW_TEV_RECORD_KNOWN_MASK;
    }
}

static void poison_indirect_subset(PCGXRawTevIndirect* raw) {
    raw->active_indirect_stage_count = UINT32_MAX;
    raw->active_indirect_stage_count_known = 2;
    raw->orders[0].value.tex_coord = UINT32_MAX;
    raw->orders[0].value.tex_map = UINT32_MAX;
    raw->orders[0].value.scale_s = UINT32_MAX;
    raw->orders[0].value.scale_t = UINT32_MAX;
    raw->orders[0].known_mask = UINT32_MAX;
    raw->matrices[0].value.s0 = INT32_MIN;
    raw->matrices[0].value.t0 = INT32_MIN;
    raw->matrices[0].value.s1 = INT32_MIN;
    raw->matrices[0].value.t1 = INT32_MIN;
    raw->matrices[0].value.s2 = INT32_MIN;
    raw->matrices[0].value.t2 = INT32_MIN;
    raw->matrices[0].value.encoded_scale = UINT32_MAX;
    raw->matrices[0].known_mask = UINT32_MAX;
}

static void init_valid_raw(
    PCGXRawTevIndirect* raw,
    uint32_t active_stage_count
) {
    uint32_t index;

    memset(raw, 0, sizeof(*raw));
    raw->active_tev_stage_count = active_stage_count;
    raw->active_tev_stage_count_known = 1;
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_STAGE_COUNT; index++) {
        fill_stage(&raw->stages[index], index);
    }
    set_stage_zero_replace(&raw->stages[0]);
    set_valid_colors(raw);
    set_valid_swaps(raw);
    poison_indirect_subset(raw);
}

static void init_gx_replace_with_unavailable_colors(
    PCGXRawTevIndirect* raw,
    uint32_t active_stage_count
) {
    uint32_t index;

    init_valid_raw(raw, active_stage_count);
    set_all_colors_unavailable(raw);
    set_stage_gx_replace(&raw->stages[0]);
    for (index = 1; index < active_stage_count; index++) {
        set_stage_gx_replace(&raw->stages[index]);
    }
}

static void set_persistent_inactive_tail(PCGXRawTevIndirect* raw) {
    PCGXRawTevStage* stage = &raw->stages[1];

    memset(stage, 0, sizeof(*stage));
    stage->value.color_a = GX_CC_CPREV;
    stage->value.k_color_sel = 12;
    stage->known_mask = PC_GX_RAW_TEV_STAGE_COLOR_A |
        PC_GX_RAW_TEV_STAGE_K_COLOR_SEL;
}

static int output_is_sentinel(
    const AcgcGxCanonicalTevState* output,
    const AcgcGxCanonicalTevState* sentinel
) {
    return memcmp(output, sentinel, sizeof(*output)) == 0;
}

static int expect_failure(const PCGXRawTevIndirect* input) {
    AcgcGxCanonicalTevState output;
    AcgcGxCanonicalTevState sentinel;
    PCGXRawTevIndirect input_before;

    memset(&output, 0xA5, sizeof(output));
    sentinel = output;
    input_before = *input;
    CHECK(!pc_gx_raw_tev_build_canonical(input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));
    CHECK(memcmp(input, &input_before, sizeof(*input)) == 0);
    return 1;
}

static int test_valid_count_one_and_persistent_tails(void) {
    AcgcGxCanonicalTevState output;
    AcgcGxCanonicalTevStage zero_stage = {0};
    PCGXRawTevIndirect raw;
    PCGXRawTevIndirect raw_before;
    uint32_t index;

    init_valid_raw(&raw, 1);
    set_persistent_inactive_tail(&raw);
    raw_before = raw;
    CHECK(pc_gx_raw_tev_build_canonical(&raw, &output));
    CHECK(memcmp(&raw, &raw_before, sizeof(raw)) == 0);
    CHECK(sizeof(output) == ACGC_GX_CANONICAL_TEV_STATE_SIZE);
    CHECK(output.header.active_stage_count == 1);
    CHECK(memcmp(&output.stages[0], &raw.stages[0].value,
                 sizeof(output.stages[0])) == 0);
    for (index = 1; index < ACGC_GX_CANONICAL_TEV_STAGE_COUNT; index++) {
        CHECK(memcmp(&output.stages[index], &zero_stage,
                     sizeof(zero_stage)) == 0);
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        CHECK(output.registers[index].r == raw.registers[index].components[0]);
        CHECK(output.registers[index].g == raw.registers[index].components[1]);
        CHECK(output.registers[index].b == raw.registers[index].components[2]);
        CHECK(output.registers[index].a == raw.registers[index].components[3]);
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        CHECK(output.konst[index].r ==
              (uint32_t)raw.konst[index].components[0]);
        CHECK(output.konst[index].g ==
              (uint32_t)raw.konst[index].components[1]);
        CHECK(output.konst[index].b ==
              (uint32_t)raw.konst[index].components[2]);
        CHECK(output.konst[index].a ==
              (uint32_t)raw.konst[index].components[3]);
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT; index++) {
        CHECK(memcmp(&output.swap_tables[index], &raw.swap_tables[index].value,
                     sizeof(output.swap_tables[index])) == 0);
    }
    CHECK(acgc_gx_canonical_tev_state_validate(&output));
    return 1;
}

static int test_valid_count_sixteen_and_decomp_blend(void) {
    AcgcGxCanonicalTevState output;
    PCGXRawTevIndirect raw;
    uint32_t index;

    init_valid_raw(&raw, ACGC_GX_CANONICAL_TEV_STAGE_COUNT);
    set_stage_one_blend(&raw.stages[1]);
    CHECK(pc_gx_raw_tev_build_canonical(&raw, &output));
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_STAGE_COUNT; index++) {
        CHECK(memcmp(&output.stages[index], &raw.stages[index].value,
                     sizeof(output.stages[index])) == 0);
    }
    CHECK(output.stages[1].color_a == GX_CC_CPREV);
    CHECK(output.stages[1].color_b == GX_CC_ONE);
    CHECK(output.stages[1].color_c == GX_CC_TEXC);
    CHECK(output.stages[1].alpha_c == GX_CA_APREV);
    CHECK(output.stages[1].color_op == GX_TEV_ADD);
    CHECK(output.stages[1].color_bias == GX_TB_ZERO);
    CHECK(output.stages[1].color_scale == GX_CS_SCALE_1);
    CHECK(output.stages[1].color_clamp == GX_TRUE);
    CHECK(output.stages[1].color_out == GX_TEVPREV);
    CHECK(acgc_gx_canonical_tev_state_validate(&output));
    return 1;
}

static int test_gx_replace_allows_unread_unavailable_colors(void) {
    AcgcGxCanonicalTevState output;
    AcgcGxCanonicalTevState output_again;
    PCGXRawTevIndirect raw;
    PCGXRawTevIndirect raw_before;
    uint32_t index;

    init_gx_replace_with_unavailable_colors(&raw, 1);
    raw_before = raw;
    CHECK(pc_gx_raw_tev_build_canonical(&raw, &output));
    CHECK(memcmp(&raw, &raw_before, sizeof(raw)) == 0);
    CHECK(output.header.active_stage_count == 1);
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_REGISTER_COUNT; index++) {
        CHECK(output.registers[index].r == 0);
        CHECK(output.registers[index].g == 0);
        CHECK(output.registers[index].b == 0);
        CHECK(output.registers[index].a == 0);
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        CHECK(output.konst[index].r == 0);
        CHECK(output.konst[index].g == 0);
        CHECK(output.konst[index].b == 0);
        CHECK(output.konst[index].a == 0);
    }
    CHECK(acgc_gx_canonical_tev_state_validate(&output));
    CHECK(pc_gx_raw_tev_build_canonical(&raw, &output_again));
    CHECK(memcmp(&output, &output_again, sizeof(output)) == 0);
    return 1;
}

static int test_stage_zero_unavailable_register_reads_fail(void) {
    static const uint32_t color_inputs[] = {
        GX_CC_CPREV, GX_CC_C0, GX_CC_C1, GX_CC_C2,
        GX_CC_APREV, GX_CC_A0, GX_CC_A1, GX_CC_A2
    };
    static const uint32_t alpha_inputs[] = {
        GX_CA_APREV, GX_CA_A0, GX_CA_A1, GX_CA_A2
    };
    PCGXRawTevIndirect raw;
    uint32_t index;

    for (index = 0; index < sizeof(color_inputs) / sizeof(color_inputs[0]);
         index++) {
        init_gx_replace_with_unavailable_colors(&raw, 1);
        raw.stages[0].value.color_a = color_inputs[index];
        CHECK(expect_failure(&raw));
    }
    for (index = 0; index < sizeof(alpha_inputs) / sizeof(alpha_inputs[0]);
         index++) {
        init_gx_replace_with_unavailable_colors(&raw, 1);
        raw.stages[0].value.alpha_a = alpha_inputs[index];
        CHECK(expect_failure(&raw));
    }
    return 1;
}

static int test_stage_order_defines_color_and_alpha_separately(void) {
    PCGXRawTevIndirect raw;

    init_gx_replace_with_unavailable_colors(&raw, 2);
    raw.stages[1].value.color_a = GX_CC_CPREV;
    raw.stages[1].value.alpha_a = GX_CA_APREV;
    CHECK(pc_gx_raw_tev_build_canonical(&raw, &(AcgcGxCanonicalTevState){0}));

    init_gx_replace_with_unavailable_colors(&raw, 2);
    raw.stages[0].value.color_out = GX_TEVREG0;
    raw.stages[0].value.alpha_out = GX_TEVPREV;
    raw.stages[1].value.color_a = GX_CC_C0;
    raw.stages[1].value.alpha_a = GX_CA_APREV;
    CHECK(pc_gx_raw_tev_build_canonical(&raw, &(AcgcGxCanonicalTevState){0}));

    raw.stages[1].value.alpha_a = GX_CA_A0;
    CHECK(!pc_gx_raw_tev_build_canonical(&raw, &(AcgcGxCanonicalTevState){0}));

    raw.stages[0].value.alpha_out = GX_TEVREG0;
    CHECK(pc_gx_raw_tev_build_canonical(&raw, &(AcgcGxCanonicalTevState){0}));
    return 1;
}

static int test_unavailable_kcolor_requires_selector_provenance(void) {
    PCGXRawTevIndirect raw;
    uint32_t index;

    for (index = 0; index < ACGC_GX_CANONICAL_TEV_KONST_COUNT; index++) {
        init_gx_replace_with_unavailable_colors(&raw, 1);
        raw.stages[0].value.color_a = GX_CC_KONST;
        raw.stages[0].value.k_color_sel = GX_TEV_KCSEL_K0 + index;
        CHECK(expect_failure(&raw));

        init_gx_replace_with_unavailable_colors(&raw, 1);
        raw.stages[0].value.alpha_a = GX_CA_KONST;
        raw.stages[0].value.k_alpha_sel = GX_TEV_KASEL_K0_R + index;
        CHECK(expect_failure(&raw));
    }

    init_gx_replace_with_unavailable_colors(&raw, 1);
    raw.stages[0].value.color_a = GX_CC_KONST;
    raw.stages[0].value.k_color_sel = GX_TEV_KCSEL_1_4;
    raw.stages[0].value.alpha_a = GX_CA_KONST;
    raw.stages[0].value.k_alpha_sel = GX_TEV_KASEL_1_4;
    CHECK(pc_gx_raw_tev_build_canonical(&raw, &(AcgcGxCanonicalTevState){0}));
    return 1;
}

static int test_null_and_count_failures(void) {
    AcgcGxCanonicalTevState output;
    AcgcGxCanonicalTevState sentinel;
    PCGXRawTevIndirect raw;

    memset(&output, 0xA5, sizeof(output));
    sentinel = output;
    CHECK(!pc_gx_raw_tev_build_canonical(NULL, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw(&raw, 1);
    CHECK(!pc_gx_raw_tev_build_canonical(&raw, NULL));

    raw.active_tev_stage_count_known = 0;
    CHECK(expect_failure(&raw));
    init_valid_raw(&raw, 1);
    raw.active_tev_stage_count_known = 2;
    CHECK(expect_failure(&raw));
    init_valid_raw(&raw, 1);
    raw.active_tev_stage_count = 0;
    CHECK(expect_failure(&raw));
    init_valid_raw(&raw, 1);
    raw.active_tev_stage_count = 17;
    CHECK(expect_failure(&raw));
    init_valid_raw(&raw, 1);
    raw.invalid = 1;
    CHECK(expect_failure(&raw));
    return 1;
}

static int test_stage_failures(void) {
    PCGXRawTevIndirect raw;

    init_valid_raw(&raw, 1);
    raw.stages[0].known_mask &= ~PC_GX_RAW_TEV_STAGE_COLOR_A;
    CHECK(expect_failure(&raw));

    init_valid_raw(&raw, 1);
    raw.stages[0].known_mask |= UINT64_C(1) << 40;
    CHECK(expect_failure(&raw));

    init_valid_raw(&raw, 1);
    raw.stages[1].known_mask |= UINT64_C(1) << 40;
    CHECK(expect_failure(&raw));

    init_valid_raw(&raw, 1);
    raw.stages[0].value.color_a = 16;
    CHECK(expect_failure(&raw));

    init_valid_raw(&raw, 1);
    raw.stages[1].known_mask = PC_GX_RAW_TEV_STAGE_COLOR_A;
    raw.stages[1].value.color_a = GX_CC_ZERO;
    raw.stages[1].value.color_b = 1;
    CHECK(expect_failure(&raw));

    init_valid_raw(&raw, 1);
    raw.stages[1].known_mask = PC_GX_RAW_TEV_STAGE_COLOR_A;
    raw.stages[1].value.color_a = 16;
    CHECK(expect_failure(&raw));

    init_valid_raw(&raw, 1);
    raw.stages[0].value.reserved[0] = 1;
    CHECK(expect_failure(&raw));
    return 1;
}

static int test_register_konst_swap_failures(void) {
    PCGXRawTevIndirect raw;

    init_gx_replace_with_unavailable_colors(&raw, 1);
    raw.registers[0].components[0] = 1;
    CHECK(expect_failure(&raw));
    init_gx_replace_with_unavailable_colors(&raw, 1);
    raw.konst[0].components[0] = 1;
    CHECK(expect_failure(&raw));

    init_valid_raw(&raw, 1);
    raw.registers[0].known_mask = 1;
    CHECK(expect_failure(&raw));
    init_valid_raw(&raw, 1);
    raw.registers[0].source = PCGX_TEV_RAW_SOURCE_MALFORMED;
    CHECK(expect_failure(&raw));
    init_valid_raw(&raw, 1);
    raw.registers[0].components[0] = -1025;
    CHECK(expect_failure(&raw));
    init_valid_raw(&raw, 1);
    raw.registers[0].components[0] = -1;
    CHECK(expect_failure(&raw));
    init_valid_raw(&raw, 1);
    raw.registers[0].reserved = 1;
    CHECK(expect_failure(&raw));
    init_valid_raw(&raw, 1);
    raw.registers[0].valid = 0;
    CHECK(expect_failure(&raw));

    init_valid_raw(&raw, 1);
    raw.konst[0].known_mask = 1;
    CHECK(expect_failure(&raw));
    init_valid_raw(&raw, 1);
    raw.konst[0].source = PCGX_TEV_RAW_SOURCE_COLOR_U8;
    CHECK(expect_failure(&raw));
    init_valid_raw(&raw, 1);
    raw.konst[0].components[0] = 256;
    CHECK(expect_failure(&raw));
    init_valid_raw(&raw, 1);
    raw.konst[0].reserved = 1;
    CHECK(expect_failure(&raw));

    init_valid_raw(&raw, 1);
    raw.swap_tables[0].known_mask = 1;
    CHECK(expect_failure(&raw));
    init_valid_raw(&raw, 1);
    raw.swap_tables[0].value.r = 4;
    CHECK(expect_failure(&raw));
    return 1;
}

int main(void) {
    if (!test_valid_count_one_and_persistent_tails() ||
        !test_valid_count_sixteen_and_decomp_blend() ||
        !test_gx_replace_allows_unread_unavailable_colors() ||
        !test_stage_zero_unavailable_register_reads_fail() ||
        !test_stage_order_defines_color_and_alpha_separately() ||
        !test_unavailable_kcolor_requires_selector_provenance() ||
        !test_null_and_count_failures() ||
        !test_stage_failures() ||
        !test_register_konst_swap_failures()) {
        return 1;
    }

    puts("pc GX canonical TEV producer fixture: PASS");
    puts("proof boundary: CPU raw-to-canonical TEV value production only; no Indirect production, packet, renderer, Metal, device, pixel, or playability claim");
    return 0;
}
