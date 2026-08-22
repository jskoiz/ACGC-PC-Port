#include "pc_gx_fog_producer.h"

int pc_gx_raw_fog_build_canonical(
    const PCGXRawFog* input,
    AcgcGxCanonicalFogState* output
) {
    AcgcGxCanonicalFogState candidate;

    if (input == NULL || output == NULL || input->invalid != 0 ||
        input->known_mask != PC_GX_RAW_FOG_KNOWN_ALL) {
        return 0;
    }

    candidate = input->value;
    if (!acgc_gx_canonical_fog_state_validate(&candidate)) return 0;

    *output = candidate;
    return 1;
}
