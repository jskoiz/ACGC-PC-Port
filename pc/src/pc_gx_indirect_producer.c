#include "pc_gx_indirect_producer.h"

#include <stdint.h>
#include <string.h>

#define PC_GX_RAW_INDIRECT_ORDER_TEX_COORD_KNOWN UINT32_C(1) << 0
#define PC_GX_RAW_INDIRECT_ORDER_TEX_MAP_KNOWN   UINT32_C(1) << 1
#define PC_GX_RAW_INDIRECT_ORDER_SCALE_S_KNOWN   UINT32_C(1) << 2
#define PC_GX_RAW_INDIRECT_ORDER_SCALE_T_KNOWN   UINT32_C(1) << 3
#define PC_GX_RAW_INDIRECT_ORDER_KNOWN_MASK \
    (PC_GX_RAW_INDIRECT_ORDER_TEX_COORD_KNOWN | \
     PC_GX_RAW_INDIRECT_ORDER_TEX_MAP_KNOWN | \
     PC_GX_RAW_INDIRECT_ORDER_SCALE_S_KNOWN | \
     PC_GX_RAW_INDIRECT_ORDER_SCALE_T_KNOWN)

static int pc_gx_raw_indirect_order_is_well_formed(
    const PCGXRawIndirectOrder* raw
) {
    uint32_t known_mask;

    if (raw == NULL) return 0;
    known_mask = raw->known_mask;
    if ((known_mask & ~PC_GX_RAW_INDIRECT_ORDER_KNOWN_MASK) != 0 ||
        raw->value.reserved[0] != 0 || raw->value.reserved[1] != 0) {
        return 0;
    }

    if ((known_mask & PC_GX_RAW_INDIRECT_ORDER_TEX_COORD_KNOWN) != 0) {
        if (raw->value.tex_coord <
                ACGC_GX_CANONICAL_INDIRECT_TEXCOORD_MIN ||
            raw->value.tex_coord >
                ACGC_GX_CANONICAL_INDIRECT_TEXCOORD_MAX) {
            return 0;
        }
    } else if (raw->value.tex_coord != 0) {
        return 0;
    }

    if ((known_mask & PC_GX_RAW_INDIRECT_ORDER_TEX_MAP_KNOWN) != 0) {
        if (raw->value.tex_map <
                ACGC_GX_CANONICAL_INDIRECT_TEXMAP_MIN ||
            raw->value.tex_map >
                ACGC_GX_CANONICAL_INDIRECT_TEXMAP_MAX) {
            return 0;
        }
    } else if (raw->value.tex_map != 0) {
        return 0;
    }

    if ((known_mask & PC_GX_RAW_INDIRECT_ORDER_SCALE_S_KNOWN) != 0) {
        if (raw->value.scale_s <
                ACGC_GX_CANONICAL_INDIRECT_SCALE_MIN ||
            raw->value.scale_s >
                ACGC_GX_CANONICAL_INDIRECT_SCALE_MAX) {
            return 0;
        }
    } else if (raw->value.scale_s != 0) {
        return 0;
    }

    if ((known_mask & PC_GX_RAW_INDIRECT_ORDER_SCALE_T_KNOWN) != 0) {
        if (raw->value.scale_t <
                ACGC_GX_CANONICAL_INDIRECT_SCALE_MIN ||
            raw->value.scale_t >
                ACGC_GX_CANONICAL_INDIRECT_SCALE_MAX) {
            return 0;
        }
    } else if (raw->value.scale_t != 0) {
        return 0;
    }

    return 1;
}

static int pc_gx_raw_indirect_matrix_is_well_formed(
    const PCGXRawIndirectMatrix* raw
) {
    uint32_t known_mask;

    if (raw == NULL) return 0;
    known_mask = raw->known_mask;
    if ((known_mask & ~PC_GX_RAW_INDIRECT_MATRIX_KNOWN_MASK) != 0) {
        return 0;
    }

    if (known_mask == 0) {
        static const AcgcGxCanonicalIndirectMatrix zero = {0};
        return memcmp(&raw->value, &zero, sizeof(zero)) == 0;
    }

    if (known_mask != PC_GX_RAW_INDIRECT_MATRIX_KNOWN_MASK ||
        raw->value.s0 < ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MIN ||
        raw->value.s0 > ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MAX ||
        raw->value.t0 < ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MIN ||
        raw->value.t0 > ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MAX ||
        raw->value.s1 < ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MIN ||
        raw->value.s1 > ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MAX ||
        raw->value.t1 < ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MIN ||
        raw->value.t1 > ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MAX ||
        raw->value.s2 < ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MIN ||
        raw->value.s2 > ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MAX ||
        raw->value.t2 < ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MIN ||
        raw->value.t2 > ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MAX ||
        raw->value.encoded_scale <
            ACGC_GX_CANONICAL_INDIRECT_ENCODED_SCALE_MIN ||
        raw->value.encoded_scale >
            ACGC_GX_CANONICAL_INDIRECT_ENCODED_SCALE_MAX ||
        raw->value.reserved != 0) {
        return 0;
    }
    return 1;
}

static void pc_gx_raw_indirect_fill_header(
    AcgcGxCanonicalIndirectState* candidate,
    uint32_t active_count
) {
    candidate->header.version =
        ACGC_GX_CANONICAL_INDIRECT_STATE_VERSION;
    candidate->header.section_id = ACGC_GX_CANONICAL_INDIRECT_SECTION_ID;
    candidate->header.section_mask = ACGC_GX_CANONICAL_INDIRECT_SECTION_MASK;
    candidate->header.byte_size = ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE;
    candidate->header.active_indirect_stage_count = active_count;
    candidate->header.order_capacity =
        ACGC_GX_CANONICAL_INDIRECT_ORDER_CAPACITY;
    candidate->header.order_record_size =
        ACGC_GX_CANONICAL_INDIRECT_ORDER_RECORD_SIZE;
    candidate->header.order_offset =
        ACGC_GX_CANONICAL_INDIRECT_ORDER_OFFSET;
    candidate->header.active_order_mask = active_count == 0 ? 0 :
        (UINT32_C(1) << active_count) - UINT32_C(1);
    candidate->header.matrix_capacity =
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_CAPACITY;
    candidate->header.matrix_record_size =
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_RECORD_SIZE;
    candidate->header.matrix_offset =
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_OFFSET;
}

int pc_gx_raw_indirect_build_canonical(
    const PCGXRawTevIndirect* input,
    AcgcGxCanonicalIndirectState* output
) {
    AcgcGxCanonicalIndirectState candidate;
    uint32_t active_count;
    uint32_t index;

    if (input == NULL || output == NULL || input->invalid != 0 ||
        input->active_indirect_stage_count_known != 1) {
        return 0;
    }

    active_count = input->active_indirect_stage_count;
    if (active_count < ACGC_GX_CANONICAL_INDIRECT_ACTIVE_STAGE_COUNT_MIN ||
        active_count > ACGC_GX_CANONICAL_INDIRECT_ACTIVE_STAGE_COUNT_MAX) {
        return 0;
    }

    for (index = 0;
         index < ACGC_GX_CANONICAL_INDIRECT_ORDER_COUNT;
         index++) {
        const PCGXRawIndirectOrder* raw = &input->orders[index];

        if (!pc_gx_raw_indirect_order_is_well_formed(raw)) return 0;
        if (index < active_count &&
            raw->known_mask != PC_GX_RAW_INDIRECT_ORDER_KNOWN_MASK) {
            return 0;
        }
    }

    for (index = 0;
         index < ACGC_GX_CANONICAL_INDIRECT_MATRIX_COUNT;
         index++) {
        if (!pc_gx_raw_indirect_matrix_is_well_formed(
                &input->matrices[index])) {
            return 0;
        }
    }

    memset(&candidate, 0, sizeof(candidate));
    pc_gx_raw_indirect_fill_header(&candidate, active_count);

    for (index = 0;
         index < active_count;
         index++) {
        candidate.orders[index] = input->orders[index].value;
    }

    for (index = 0;
         index < ACGC_GX_CANONICAL_INDIRECT_MATRIX_COUNT;
         index++) {
        if (input->matrices[index].known_mask ==
                PC_GX_RAW_INDIRECT_MATRIX_KNOWN_MASK) {
            candidate.matrices[index] = input->matrices[index].value;
            candidate.header.matrix_valid_mask |= UINT32_C(1) << index;
        }
    }

    if (!acgc_gx_canonical_indirect_state_validate(&candidate)) return 0;

    *output = candidate;
    return 1;
}
