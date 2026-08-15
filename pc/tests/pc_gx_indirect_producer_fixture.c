#include "pc_gx_indirect_producer.h"

#include <stdint.h>
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

#define RAW_ORDER_TEX_COORD_KNOWN UINT32_C(1) << 0
#define RAW_ORDER_TEX_MAP_KNOWN   UINT32_C(1) << 1
#define RAW_ORDER_SCALE_S_KNOWN   UINT32_C(1) << 2
#define RAW_ORDER_SCALE_T_KNOWN   UINT32_C(1) << 3
#define RAW_ORDER_ALL_KNOWN \
    (RAW_ORDER_TEX_COORD_KNOWN | RAW_ORDER_TEX_MAP_KNOWN | \
     RAW_ORDER_SCALE_S_KNOWN | RAW_ORDER_SCALE_T_KNOWN)

static void init_raw(
    PCGXRawTevIndirect* input,
    uint32_t active_count
) {
    memset(input, 0, sizeof(*input));
    input->active_indirect_stage_count = active_count;
    input->active_indirect_stage_count_known = 1;
}

static void set_order(
    PCGXRawTevIndirect* input,
    uint32_t index,
    uint32_t known_mask,
    uint32_t tex_coord,
    uint32_t tex_map,
    uint32_t scale_s,
    uint32_t scale_t
) {
    PCGXRawIndirectOrder* order = &input->orders[index];

    memset(order, 0, sizeof(*order));
    order->known_mask = known_mask;
    if ((known_mask & RAW_ORDER_TEX_COORD_KNOWN) != 0) {
        order->value.tex_coord = tex_coord;
    }
    if ((known_mask & RAW_ORDER_TEX_MAP_KNOWN) != 0) {
        order->value.tex_map = tex_map;
    }
    if ((known_mask & RAW_ORDER_SCALE_S_KNOWN) != 0) {
        order->value.scale_s = scale_s;
    }
    if ((known_mask & RAW_ORDER_SCALE_T_KNOWN) != 0) {
        order->value.scale_t = scale_t;
    }
}

static void set_full_order(
    PCGXRawTevIndirect* input,
    uint32_t index,
    uint32_t tex_coord,
    uint32_t tex_map,
    uint32_t scale_s,
    uint32_t scale_t
) {
    set_order(input, index, RAW_ORDER_ALL_KNOWN,
              tex_coord, tex_map, scale_s, scale_t);
}

static void set_matrix(
    PCGXRawTevIndirect* input,
    uint32_t index,
    uint32_t known_mask,
    int32_t s0,
    int32_t t0,
    int32_t s1,
    int32_t t1,
    int32_t s2,
    int32_t t2,
    uint32_t encoded_scale
) {
    PCGXRawIndirectMatrix* matrix = &input->matrices[index];

    memset(matrix, 0, sizeof(*matrix));
    matrix->known_mask = known_mask;
    if (known_mask == PC_GX_RAW_INDIRECT_MATRIX_KNOWN_MASK) {
        matrix->value.s0 = s0;
        matrix->value.t0 = t0;
        matrix->value.s1 = s1;
        matrix->value.t1 = t1;
        matrix->value.s2 = s2;
        matrix->value.t2 = t2;
        matrix->value.encoded_scale = encoded_scale;
    } else if ((known_mask & PC_GX_RAW_INDIRECT_MATRIX_S0_KNOWN) != 0) {
        matrix->value.s0 = s0;
    }
}

static void fill_valid_raw(
    PCGXRawTevIndirect* input,
    uint32_t active_count
) {
    uint32_t index;

    init_raw(input, active_count);
    for (index = 0; index < active_count; index++) {
        set_full_order(input, index, index, index + 1,
                       index == 0 ? 0 : 8, index == 3 ? 8 : 1);
    }
    set_matrix(input, 0, PC_GX_RAW_INDIRECT_MATRIX_KNOWN_MASK,
               0, 1, -2, 3, -4, 5, 6);
}

static void init_output_sentinel(
    AcgcGxCanonicalIndirectState* output
) {
    memset(output, 0xA5, sizeof(*output));
}

static int output_is_sentinel(
    const AcgcGxCanonicalIndirectState* output,
    const AcgcGxCanonicalIndirectState* sentinel
) {
    return memcmp(output, sentinel, sizeof(*output)) == 0;
}

static int test_null_unknown_and_sticky_fail_closed(void) {
    PCGXRawTevIndirect input;
    AcgcGxCanonicalIndirectState output;
    AcgcGxCanonicalIndirectState sentinel;

    init_raw(&input, 0);
    init_output_sentinel(&output);
    sentinel = output;

    CHECK(!pc_gx_raw_indirect_build_canonical(NULL, &output));
    CHECK(output_is_sentinel(&output, &sentinel));
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, NULL));

    input.active_indirect_stage_count_known = 0;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    input.active_indirect_stage_count_known = 2;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    input.active_indirect_stage_count_known = 1;
    input.invalid = 1;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));
    return 1;
}

static int test_count_boundaries_and_active_orders(void) {
    PCGXRawTevIndirect input;
    AcgcGxCanonicalIndirectState output;
    uint32_t count;

    for (count = 0; count <= 4; count++) {
        uint32_t index;

        fill_valid_raw(&input, count);
        for (index = count; index < 4; index++) {
            set_full_order(&input, index, 7 - index, index,
                           index, 8 - index);
        }
        CHECK(pc_gx_raw_indirect_build_canonical(&input, &output));
        CHECK(output.header.active_indirect_stage_count == count);
        CHECK(output.header.active_order_mask ==
              (count == 0 ? 0 : (UINT32_C(1) << count) - 1));
        for (index = 0; index < 4; index++) {
            if (index < count) {
                CHECK(output.orders[index].tex_coord == index);
                CHECK(output.orders[index].tex_map == index + 1);
            } else {
                static const AcgcGxCanonicalIndirectOrder zero = {0};
                CHECK(memcmp(&output.orders[index], &zero,
                             sizeof(zero)) == 0);
            }
        }
    }

    init_raw(&input, 5);
    init_output_sentinel(&output);
    {
        AcgcGxCanonicalIndirectState sentinel = output;
        CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
        CHECK(output_is_sentinel(&output, &sentinel));
    }
    return 1;
}

static int test_partial_and_complete_inactive_persistence(void) {
    PCGXRawTevIndirect input;
    PCGXRawTevIndirect input_before;
    AcgcGxCanonicalIndirectState output;
    static const AcgcGxCanonicalIndirectOrder zero_order = {0};

    init_raw(&input, 1);
    set_full_order(&input, 0, 2, 3, 4, 5);
    set_order(&input, 1, RAW_ORDER_TEX_COORD_KNOWN,
              7, 0, 0, 0);
    set_full_order(&input, 2, 1, 2, 8, 8);
    set_order(&input, 3,
              RAW_ORDER_SCALE_S_KNOWN | RAW_ORDER_SCALE_T_KNOWN,
              0, 0, 0, 8);
    input_before = input;

    CHECK(pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(memcmp(&input, &input_before, sizeof(input)) == 0);
    CHECK(output.orders[0].tex_coord == 2);
    CHECK(memcmp(&output.orders[1], &zero_order, sizeof(zero_order)) == 0);
    CHECK(memcmp(&output.orders[2], &zero_order, sizeof(zero_order)) == 0);
    CHECK(memcmp(&output.orders[3], &zero_order, sizeof(zero_order)) == 0);
    return 1;
}

static int test_count_zero_needs_no_order_completeness(void) {
    PCGXRawTevIndirect input;
    AcgcGxCanonicalIndirectState output;
    static const AcgcGxCanonicalIndirectOrder zero_order = {0};

    init_raw(&input, 0);
    set_order(&input, 0, RAW_ORDER_TEX_COORD_KNOWN, 7, 0, 0, 0);
    set_full_order(&input, 1, 1, 2, 8, 1);
    set_matrix(&input, 2, PC_GX_RAW_INDIRECT_MATRIX_KNOWN_MASK,
               -1024, 1023, -1024, 1023, -1024, 1023, 0);

    CHECK(pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output.header.active_indirect_stage_count == 0);
    CHECK(output.header.active_order_mask == 0);
    CHECK(memcmp(&output.orders[0], &zero_order, sizeof(zero_order)) == 0);
    CHECK(memcmp(&output.orders[1], &zero_order, sizeof(zero_order)) == 0);
    CHECK(output.header.matrix_valid_mask == (UINT32_C(1) << 2));
    CHECK(output.matrices[2].s0 == -1024);
    CHECK(output.matrices[2].t0 == 1023);
    CHECK(output.matrices[2].encoded_scale == 0);
    return 1;
}

static int test_exact_scales_and_matrix_boundaries(void) {
    PCGXRawTevIndirect input;
    AcgcGxCanonicalIndirectState output;

    init_raw(&input, 4);
    set_full_order(&input, 0, 0, 0, 0, 0);
    set_full_order(&input, 1, 1, 1, 1, 1);
    set_full_order(&input, 2, 2, 2, 7, 8);
    set_full_order(&input, 3, 7, 7, 8, 7);
    set_matrix(&input, 0, PC_GX_RAW_INDIRECT_MATRIX_KNOWN_MASK,
               -1024, 1023, -1024, 1023, -1024, 1023, 0);
    set_matrix(&input, 1, PC_GX_RAW_INDIRECT_MATRIX_KNOWN_MASK,
               1023, -1024, 1023, -1024, 1023, -1024, 63);
    set_matrix(&input, 2, PC_GX_RAW_INDIRECT_MATRIX_KNOWN_MASK,
               0, 0, 0, 0, 0, 0, 1);

    CHECK(pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output.orders[0].scale_s == 0);
    CHECK(output.orders[2].scale_s == 7);
    CHECK(output.orders[2].scale_t == 8);
    CHECK(output.orders[3].scale_s == 8);
    CHECK(output.header.matrix_valid_mask == UINT32_C(0x7));
    CHECK(output.matrices[0].s0 == -1024);
    CHECK(output.matrices[0].t0 == 1023);
    CHECK(output.matrices[1].s0 == 1023);
    CHECK(output.matrices[1].t0 == -1024);
    CHECK(output.matrices[0].encoded_scale == 0);
    CHECK(output.matrices[1].encoded_scale == 63);
    return 1;
}

static int test_matrix_absent_and_provenance(void) {
    PCGXRawTevIndirect input;
    AcgcGxCanonicalIndirectState output;
    AcgcGxCanonicalIndirectState sentinel;

    fill_valid_raw(&input, 1);
    set_matrix(&input, 0, PC_GX_RAW_INDIRECT_MATRIX_S0_KNOWN,
               1, 0, 0, 0, 0, 0, 0);
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    fill_valid_raw(&input, 1);
    input.matrices[0].known_mask = UINT32_C(0x80);
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    fill_valid_raw(&input, 1);
    input.matrices[0].known_mask = 0;
    input.matrices[0].value.s0 = 1;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));
    return 1;
}

static int test_invalid_order_masks_reserved_and_domains(void) {
    PCGXRawTevIndirect input;
    AcgcGxCanonicalIndirectState output;
    AcgcGxCanonicalIndirectState sentinel;

    fill_valid_raw(&input, 1);
    input.orders[0].known_mask = RAW_ORDER_ALL_KNOWN | UINT32_C(0x10);
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    fill_valid_raw(&input, 1);
    input.orders[1].value.reserved[0] = 1;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    fill_valid_raw(&input, 1);
    input.orders[0].value.tex_coord = 8;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    fill_valid_raw(&input, 1);
    input.orders[0].value.scale_t = 9;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    fill_valid_raw(&input, 1);
    input.orders[0].known_mask = RAW_ORDER_TEX_COORD_KNOWN |
        RAW_ORDER_TEX_MAP_KNOWN | RAW_ORDER_SCALE_S_KNOWN;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));
    return 1;
}

static int test_invalid_matrix_values_reserved_and_output_atomicity(void) {
    PCGXRawTevIndirect input;
    AcgcGxCanonicalIndirectState output;
    AcgcGxCanonicalIndirectState sentinel;

    fill_valid_raw(&input, 1);
    input.matrices[0].value.s0 = -1025;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    fill_valid_raw(&input, 1);
    input.matrices[0].value.t2 = 1024;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    fill_valid_raw(&input, 1);
    input.matrices[0].value.encoded_scale = 64;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    fill_valid_raw(&input, 1);
    input.matrices[0].value.reserved = 1;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));
    return 1;
}

static int test_tev_fields_are_not_owned_here(void) {
    PCGXRawTevIndirect input;
    PCGXRawTevIndirect input_before;
    AcgcGxCanonicalIndirectState output;

    fill_valid_raw(&input, 1);
    input.active_tev_stage_count = UINT32_MAX;
    input.active_tev_stage_count_known = UINT32_MAX;
    input.stages[0].value.ind_stage = UINT32_MAX;
    input.stages[0].known_mask = UINT64_MAX;
    input.registers[0].components[0] = INT32_MIN;
    input.konst[0].components[1] = INT32_MAX;
    input.swap_tables[0].value.r = UINT32_MAX;
    input_before = input;

    CHECK(pc_gx_raw_indirect_build_canonical(&input, &output));
    CHECK(memcmp(&input, &input_before, sizeof(input)) == 0);
    CHECK(output.header.active_indirect_stage_count == 1);
    CHECK(output.header.matrix_valid_mask == 1);
    CHECK(output.matrices[0].s0 == 0);
    CHECK(output.matrices[0].encoded_scale == 6);
    return 1;
}

static int test_success_is_destination_and_input_safe(void) {
    PCGXRawTevIndirect input;
    PCGXRawTevIndirect input_before;
    AcgcGxCanonicalIndirectState first;
    AcgcGxCanonicalIndirectState second;

    fill_valid_raw(&input, 2);
    set_full_order(&input, 0, 3, 4, 8, 0);
    set_full_order(&input, 1, 4, 5, 0, 8);
    input_before = input;
    CHECK(pc_gx_raw_indirect_build_canonical(&input, &first));
    CHECK(memcmp(&input, &input_before, sizeof(input)) == 0);
    CHECK(pc_gx_raw_indirect_build_canonical(&input, &second));
    CHECK(memcmp(&first, &second, sizeof(first)) == 0);
    CHECK(acgc_gx_canonical_indirect_state_validate(&first));
    return 1;
}

int main(void) {
    if (!test_null_unknown_and_sticky_fail_closed() ||
        !test_count_boundaries_and_active_orders() ||
        !test_partial_and_complete_inactive_persistence() ||
        !test_count_zero_needs_no_order_completeness() ||
        !test_exact_scales_and_matrix_boundaries() ||
        !test_matrix_absent_and_provenance() ||
        !test_invalid_order_masks_reserved_and_domains() ||
        !test_invalid_matrix_values_reserved_and_output_atomicity() ||
        !test_tev_fields_are_not_owned_here() ||
        !test_success_is_destination_and_input_safe()) {
        return 1;
    }
    puts("pc GX raw Indirect canonical producer fixture: PASS");
    return 0;
}
