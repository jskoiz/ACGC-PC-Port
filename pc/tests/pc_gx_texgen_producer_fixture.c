#include "pc_gx_texgen_producer.h"

#include <dolphin/gx/GXEnum.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static uint32_t finite_word(uint32_t index) {
    static const uint32_t words[] = {
        UINT32_C(0x00000000), UINT32_C(0x80000000), UINT32_C(0x3F800000),
        UINT32_C(0xBF800000), UINT32_C(0x40000000), UINT32_C(0xC0000000),
        UINT32_C(0x40400000), UINT32_C(0xC0400000), UINT32_C(0x41000000),
        UINT32_C(0xC1000000), UINT32_C(0x7F7FFFFF), UINT32_C(0xFF7FFFFF)
    };

    return words[index % (sizeof(words) / sizeof(words[0]))];
}

static void fill_matrix(
    PCGXRawTexMatrix* record,
    uint32_t logical_id,
    uint32_t seed
) {
    uint32_t word;

    memset(record, 0, sizeof(*record));
    record->logical_id = logical_id;
    record->slot_known = 1;
    record->provenance = PC_GX_TEXGEN_MATRIX_PROVENANCE_IMMEDIATE;
    record->last_load_type = GX_MTX3x4;
    record->last_written_word_count = 12;
    record->known_word_mask =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4;
    for (word = 0; word < PC_GX_TEXGEN_MATRIX_WORD_COUNT; word++) {
        record->words[word] = finite_word(seed + word);
    }
}

static void fill_texgen_record(
    PCGXRawTexgenRecord* record,
    uint32_t function,
    uint32_t source,
    uint32_t ordinary_matrix_id,
    uint32_t normalize,
    uint32_t post_matrix_id
) {
    memset(record, 0, sizeof(*record));
    record->function = function;
    record->source = source;
    record->ordinary_matrix_id = ordinary_matrix_id;
    record->normalize = normalize;
    record->post_matrix_id = post_matrix_id;
    record->component_known = PC_GX_TEXGEN_KNOWN_ALL;
}

static void init_valid_raw(PCGXRawTexgen* input) {
    uint32_t index;

    memset(input, 0, sizeof(*input));
    input->active_texgen_count = 5;
    input->active_texgen_count_known = 1;

    for (index = 0; index < PC_GX_TEXGEN_ORDINARY_MATRIX_COUNT; index++) {
        uint32_t logical_id = index < 10u ?
            (uint32_t)(GX_TEXMTX0 + index * 3u) :
            (uint32_t)GX_IDENTITY;

        fill_matrix(&input->ordinary[index], logical_id, index);
    }
    for (index = 0; index < PC_GX_TEXGEN_POST_MATRIX_COUNT; index++) {
        uint32_t logical_id = index < 20u ?
            (uint32_t)(GX_PTTEXMTX0 + index * 3u) :
            (uint32_t)GX_PTIDENTITY;

        fill_matrix(&input->post[index], logical_id, index + 32u);
    }
    for (index = 0; index < PC_GX_TEXGEN_COUNT; index++) {
        fill_texgen_record(
            &input->texgen[index],
            GX_TG_MTX3x4,
            GX_TG_POS,
            (uint32_t)(GX_TEXMTX0 + (index % 3u) * 3u),
            index & 1u,
            (uint32_t)(GX_PTTEXMTX0 + (index % 3u) * 3u)
        );
        input->su[index].component_known =
            PC_GX_TEXGEN_SU_KNOWN_MANUAL |
            PC_GX_TEXGEN_SU_KNOWN_SCALE_S |
            PC_GX_TEXGEN_SU_KNOWN_SCALE_T |
            PC_GX_TEXGEN_SU_KNOWN_BIAS_S |
            PC_GX_TEXGEN_SU_KNOWN_BIAS_T |
            PC_GX_TEXGEN_SU_KNOWN_CYLINDER_S |
            PC_GX_TEXGEN_SU_KNOWN_CYLINDER_T;
        input->su[index].manual_enable = index & 1u;
        input->su[index].scale_s_raw_u16 = (uint16_t)(0x0100u + index);
        input->su[index].scale_t_raw_u16 = (uint16_t)(0x0200u + index);
        input->su[index].bias_s = index & 1u;
        input->su[index].bias_t = (index + 1u) & 1u;
        input->su[index].cylinder_s = index & 1u;
        input->su[index].cylinder_t = (index + 1u) & 1u;
    }

    fill_texgen_record(
        &input->texgen[0], GX_TG_MTX3x4, GX_TG_POS,
        GX_TEXMTX0, GX_FALSE, GX_PTTEXMTX0
    );
    fill_texgen_record(
        &input->texgen[1], GX_TG_MTX3x4, GX_TG_TEX0,
        GX_TEXMTX1, GX_TRUE, GX_PTTEXMTX1
    );
    fill_texgen_record(
        &input->texgen[2], GX_TG_BUMP0, GX_TG_TEXCOORD0,
        GX_TEXMTX2, GX_FALSE, GX_PTTEXMTX2
    );
    fill_texgen_record(
        &input->texgen[3], GX_TG_SRTG, GX_TG_COLOR0,
        GX_TEXMTX0, GX_FALSE, GX_PTTEXMTX0
    );
    fill_texgen_record(
        &input->texgen[4], GX_TG_SRTG, GX_TG_COLOR1,
        GX_TEXMTX1, GX_TRUE, GX_PTTEXMTX1
    );
}

static int output_is_unchanged(
    const AcgcGxCanonicalTexgenState* output,
    const AcgcGxCanonicalTexgenState* before
) {
    return memcmp(output, before, sizeof(*output)) == 0;
}

static void init_output_sentinel(
    AcgcGxCanonicalTexgenState* output
) {
    memset(output, 0xA5, sizeof(*output));
}

static int expect_failure(const PCGXRawTexgen* input) {
    AcgcGxCanonicalTexgenState output;
    AcgcGxCanonicalTexgenState before;

    init_output_sentinel(&output);
    before = output;
    CHECK(!pc_gx_raw_texgen_build_canonical(input, &output));
    CHECK(output_is_unchanged(&output, &before));
    return 0;
}

static int test_layout_and_valid_phases(void) {
    PCGXRawTexgen input;
    PCGXRawTexgen input_before;
    AcgcGxCanonicalTexgenState output;

    CHECK(sizeof(AcgcGxCanonicalTexgenState) == 0xA40);
    CHECK(_Alignof(AcgcGxCanonicalTexgenState) == 4);
    CHECK(offsetof(AcgcGxCanonicalTexgenState, header) == 0x000);
    CHECK(offsetof(AcgcGxCanonicalTexgenState, texgen) == 0x040);
    CHECK(offsetof(AcgcGxCanonicalTexgenState, ordinary_matrix) == 0x140);
    CHECK(offsetof(AcgcGxCanonicalTexgenState, post_matrix) == 0x400);
    CHECK(offsetof(AcgcGxCanonicalTexgenState, su) == 0x940);

    init_valid_raw(&input);
    input_before = input;
    init_output_sentinel(&output);
    CHECK(pc_gx_raw_texgen_build_canonical(&input, &output));
    CHECK(memcmp(&input, &input_before, sizeof(input)) == 0);
    CHECK(output.header.active_texgen_count == 5);
    CHECK(output.header.texgen_capacity == 8);
    CHECK(output.header.known_texgen_count == 8);
    CHECK(output.header.ordinary_matrix_count == 11);
    CHECK(output.header.post_matrix_count == 21);
    CHECK(output.header.su_count == 8);
    CHECK(output.header.texgen_known_mask == UINT32_C(0xFF));
    CHECK(output.header.ordinary_matrix_known_mask == UINT32_C(0x7FF));
    CHECK(output.header.post_matrix_known_mask == UINT32_C(0x1FFFFF));
    CHECK(output.header.su_known_mask == UINT32_C(0xFF));
    CHECK(output.header.component_known_summary == UINT32_C(0x0F));

    CHECK(output.texgen[0].function == GX_TG_MTX3x4);
    CHECK(output.texgen[0].source == GX_TG_POS);
    CHECK(output.texgen[1].source == GX_TG_TEX0);
    CHECK(output.texgen[2].function == GX_TG_BUMP0);
    CHECK(output.texgen[2].source == GX_TG_TEXCOORD0);
    CHECK(output.texgen[3].function == GX_TG_SRTG);
    CHECK(output.texgen[3].source == GX_TG_COLOR0);
    CHECK(output.texgen[4].source == GX_TG_COLOR1);
    CHECK(output.texgen[4].normalize == GX_TRUE);
    CHECK(output.texgen[0].reserved[0] == 0);
    CHECK(output.texgen[0].reserved[1] == 0);

    CHECK(output.ordinary_matrix[0].logical_id == GX_TEXMTX0);
    CHECK(output.ordinary_matrix[0].last_load_type == GX_MTX3x4);
    CHECK(output.ordinary_matrix[0].last_written_word_count == 12);
    CHECK(output.ordinary_matrix[0].known_word_mask == UINT32_C(0xFFF));
    CHECK(output.ordinary_matrix[0].words[0] == input.ordinary[0].words[0]);
    CHECK(output.post_matrix[20].logical_id == GX_PTIDENTITY);
    CHECK(output.post_matrix[20].words[11] == input.post[20].words[11]);
    CHECK(output.su[0].component_known == UINT32_C(0x7F));
    CHECK(output.su[0].scale_s_raw_u16 == input.su[0].scale_s_raw_u16);
    CHECK(output.su[0].scale_t_raw_u16 == input.su[0].scale_t_raw_u16);
    CHECK(output.su[0].manual_enable == input.su[0].manual_enable);
    CHECK(acgc_gx_canonical_texgen_state_validate(&output));
    return 0;
}

static int test_inactive_records_and_2x4_range(void) {
    PCGXRawTexgen input;
    AcgcGxCanonicalTexgenState output;
    uint32_t index;

    init_valid_raw(&input);
    input.active_texgen_count = 1;
    for (index = 1; index < PC_GX_TEXGEN_COUNT; index++) {
        memset(&input.texgen[index], 0, sizeof(input.texgen[index]));
    }
    input.texgen[0].function = GX_TG_MTX2x4;
    input.ordinary[0].last_load_type = GX_MTX2x4;
    input.ordinary[0].last_written_word_count = 8;
    input.ordinary[0].known_word_mask =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_2X4;
    memset(&input.ordinary[0].words[8], 0,
           4 * sizeof(input.ordinary[0].words[0]));

    CHECK(pc_gx_raw_texgen_build_canonical(&input, &output));
    CHECK(output.header.active_texgen_count == 1);
    CHECK(output.header.texgen_known_mask == 1);
    CHECK(output.header.known_texgen_count == 1);
    CHECK(output.texgen[1].component_known == 0);
    CHECK(output.texgen[1].function == 0);
    CHECK(output.ordinary_matrix[0].last_load_type == GX_MTX2x4);
    CHECK(output.ordinary_matrix[0].last_written_word_count == 8);
    CHECK(output.ordinary_matrix[0].known_word_mask == UINT32_C(0xFF));
    CHECK(output.ordinary_matrix[0].words[8] == 0);
    CHECK((output.header.component_known_summary &
           ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_ORDINARY_MATRIX) == 0);
    CHECK(acgc_gx_canonical_texgen_state_validate(&output));
    return 0;
}

static int test_partial_su_and_inactive_indexed_matrix(void) {
    PCGXRawTexgen input;
    AcgcGxCanonicalTexgenState output;

    init_valid_raw(&input);
    input.active_texgen_count = 0;
    memset(&input.su[0], 0, sizeof(input.su[0]));
    input.su[0].component_known =
        PC_GX_TEXGEN_SU_KNOWN_MANUAL |
        PC_GX_TEXGEN_SU_KNOWN_SCALE_S |
        PC_GX_TEXGEN_SU_KNOWN_SCALE_T;
    input.su[0].manual_enable = GX_TRUE;
    input.su[0].scale_s_raw_u16 = 0xFFFFu;
    input.su[0].scale_t_raw_u16 = 0x0101u;
    memset(&input.su[1], 0, sizeof(input.su[1]));
    input.su[1].component_known =
        PC_GX_TEXGEN_SU_KNOWN_BIAS_S |
        PC_GX_TEXGEN_SU_KNOWN_BIAS_T |
        PC_GX_TEXGEN_SU_KNOWN_CYLINDER_S |
        PC_GX_TEXGEN_SU_KNOWN_CYLINDER_T;
    input.su[1].bias_s = GX_TRUE;
    input.su[1].bias_t = GX_FALSE;
    input.su[1].cylinder_s = GX_FALSE;
    input.su[1].cylinder_t = GX_TRUE;

    CHECK(pc_gx_raw_texgen_build_canonical(&input, &output));
    CHECK(output.su[0].component_known == UINT32_C(0x07));
    CHECK(output.su[0].scale_s_raw_u16 == UINT32_C(0xFFFF));
    CHECK(output.su[0].scale_t_raw_u16 == UINT32_C(0x0101));
    CHECK(output.su[0].bias_s == 0);
    CHECK(output.su[1].component_known == UINT32_C(0x78));
    CHECK(output.su[1].bias_s == 1);
    CHECK(output.su[1].cylinder_t == 1);
    CHECK((output.header.component_known_summary &
           ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_SU) == 0);

    init_valid_raw(&input);
    input.active_texgen_count = 0;
    input.ordinary[0].provenance =
        PC_GX_TEXGEN_MATRIX_PROVENANCE_INDEXED_UNRESOLVED;
    input.ordinary[0].known_word_mask = 0;
    memset(input.ordinary[0].words, 0, sizeof(input.ordinary[0].words));
    CHECK(pc_gx_raw_texgen_build_canonical(&input, &output));
    CHECK(output.ordinary_matrix[0].last_load_type == GX_MTX3x4);
    CHECK(output.ordinary_matrix[0].last_written_word_count == 12);
    CHECK(output.ordinary_matrix[0].known_word_mask == 0);
    CHECK((output.header.component_known_summary &
           ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_ORDINARY_MATRIX) == 0);

    input.active_texgen_count = 1;
    CHECK(expect_failure(&input) == 0);
    return 0;
}

static int test_inactive_matrix_attempted_range_provenance_is_strict(void) {
    PCGXRawTexgen input;

    init_valid_raw(&input);
    input.active_texgen_count = 0;
    input.ordinary[0].known_word_mask &= ~UINT32_C(1);
    input.ordinary[0].words[0] = 0;
    CHECK(expect_failure(&input) == 0);

    init_valid_raw(&input);
    input.active_texgen_count = 0;
    input.ordinary[0].provenance =
        PC_GX_TEXGEN_MATRIX_PROVENANCE_INDEXED_UNRESOLVED;
    input.ordinary[0].known_word_mask = UINT32_C(1);
    memset(input.ordinary[0].words, 0, sizeof(input.ordinary[0].words));
    input.ordinary[0].words[0] = finite_word(0);
    CHECK(expect_failure(&input) == 0);
    return 0;
}

static int test_fail_closed_domains_and_output_preservation(void) {
    PCGXRawTexgen base;
    PCGXRawTexgen input;

    init_valid_raw(&base);

    CHECK(expect_failure(NULL) == 0);
    input = base;
    input.active_texgen_count_known = 0;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.active_texgen_count_known = 2;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.invalid = 1;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.active_texgen_count = 9;
    CHECK(expect_failure(&input) == 0);

    input = base;
    input.texgen[0].component_known = 1;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.texgen[0].function = 11;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.texgen[0].source = UINT32_MAX;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.texgen[0].ordinary_matrix_id = 31;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.texgen[0].post_matrix_id = 66;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.texgen[0].normalize = 2;
    CHECK(expect_failure(&input) == 0);

    input = base;
    input.ordinary[0].logical_id = 31;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.ordinary[0].slot_known = 2;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.ordinary[0].known_word_mask = UINT32_C(0x1000);
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.ordinary[0].known_word_mask &= ~UINT32_C(1);
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.ordinary[0].words[0] = UINT32_C(0x7F800000);
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.ordinary[0].last_written_word_count = 7;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.ordinary[0].last_written_word_count = 12;
    input.ordinary[0].last_load_type = GX_MTX2x4;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.ordinary[0].provenance =
        PC_GX_TEXGEN_MATRIX_PROVENANCE_INVALID;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.ordinary[0].provenance =
        PC_GX_TEXGEN_MATRIX_PROVENANCE_INDEXED_UNRESOLVED;
    CHECK(expect_failure(&input) == 0);

    input = base;
    input.post[0].last_load_type = GX_MTX2x4;
    CHECK(expect_failure(&input) == 0);

    input = base;
    input.su[0].component_known = UINT32_C(0x80);
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.su[0].component_known = 0;
    input.su[0].manual_enable = 1;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.su[0].manual_enable = 2;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.su[0].bias_t = UINT32_MAX;
    CHECK(expect_failure(&input) == 0);
    input = base;
    input.su[0].cylinder_s = 2;
    CHECK(expect_failure(&input) == 0);

    input = base;
    input.ordinary[0].provenance =
        PC_GX_TEXGEN_MATRIX_PROVENANCE_INDEXED_UNRESOLVED;
    input.ordinary[0].known_word_mask = 0;
    memset(input.ordinary[0].words, 0, sizeof(input.ordinary[0].words));
    input.active_texgen_count = 1;
    CHECK(expect_failure(&input) == 0);
    return 0;
}

static int test_null_output_and_repeatability(void) {
    PCGXRawTexgen input;
    PCGXRawTexgen before;
    AcgcGxCanonicalTexgenState first;
    AcgcGxCanonicalTexgenState second;

    init_valid_raw(&input);
    before = input;
    CHECK(!pc_gx_raw_texgen_build_canonical(&input, NULL));
    CHECK(pc_gx_raw_texgen_build_canonical(&input, &first));
    CHECK(memcmp(&input, &before, sizeof(input)) == 0);
    memset(&second, 0x5A, sizeof(second));
    CHECK(pc_gx_raw_texgen_build_canonical(&input, &second));
    CHECK(memcmp(&first, &second, sizeof(first)) == 0);
    return 0;
}

int main(void) {
    if (test_layout_and_valid_phases() != 0 ||
        test_inactive_records_and_2x4_range() != 0 ||
        test_partial_su_and_inactive_indexed_matrix() != 0 ||
        test_inactive_matrix_attempted_range_provenance_is_strict() != 0 ||
        test_fail_closed_domains_and_output_preservation() != 0 ||
        test_null_output_and_repeatability() != 0) {
        return 1;
    }

    puts("pc GX raw Texgen/SU producer fixture: PASS");
    puts("proof boundary: setter-owned CPU Texgen/SU snapshot conversion and canonical validation only; no renderer, Metal, device, pixel, or playability claim");
    return 0;
}
