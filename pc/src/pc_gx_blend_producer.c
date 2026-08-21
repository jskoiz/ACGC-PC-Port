#include "pc_gx_blend_producer.h"

#include <stdint.h>
#include <string.h>

static int pc_gx_raw_blend_reserved_is_zero(
    const PCGXRawBlend* input
) {
    return input->reserved[0] == 0 &&
        input->reserved[1] == 0;
}

int pc_gx_raw_blend_build_canonical(
    const PCGXRawBlend* input,
    AcgcGxCanonicalBlendState* output
) {
    AcgcGxCanonicalBlendState candidate;

    if (input == NULL || output == NULL ||
        input->known != UINT8_C(1) || input->invalid != 0 ||
        !pc_gx_raw_blend_reserved_is_zero(input)) {
        return 0;
    }

    /* Keep the leaf producer explicit about inactive fields: canonical Blend
     * preserves all four setter words rather than deriving or normalizing
     * factors based on the selected mode. */
    memset(&candidate, 0, sizeof(candidate));
    candidate.mode = input->value.mode;
    candidate.source_factor = input->value.source_factor;
    candidate.destination_factor = input->value.destination_factor;
    candidate.logic_op = input->value.logic_op;

    if (!acgc_gx_canonical_blend_state_validate(&candidate)) return 0;

    *output = candidate;
    return 1;
}
