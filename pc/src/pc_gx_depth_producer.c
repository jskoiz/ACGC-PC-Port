#include "pc_gx_depth_producer.h"

#include <stdint.h>
#include <string.h>

static int pc_gx_raw_depth_reserved_is_zero(
    const PCGXRawDepth* input
) {
    return input->reserved[0] == 0 &&
        input->reserved[1] == 0 &&
        input->reserved[2] == 0;
}

int pc_gx_raw_depth_build_canonical(
    const PCGXRawDepth* input,
    AcgcGxCanonicalDepthState* output
) {
    AcgcGxCanonicalDepthState candidate;

    if (input == NULL || output == NULL ||
        input->known != UINT8_C(1) ||
        !pc_gx_raw_depth_reserved_is_zero(input)) {
        return 0;
    }

    /* The raw snapshot has no separate invalid bit.  The exact knownness and
     * reserved-byte checks above reject every noncanonical provenance state;
     * the existing canonical validator owns all value-domain checks below. */
    memset(&candidate, 0, sizeof(candidate));
    candidate.z_compare_enable = input->compare_enable;
    candidate.z_compare_func = input->compare_func;
    candidate.z_update_enable = input->update_enable;

    if (!acgc_gx_canonical_depth_state_validate(&candidate)) return 0;

    *output = candidate;
    return 1;
}
