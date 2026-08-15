#ifndef PC_GX_TEXGEN_PRODUCER_H
#define PC_GX_TEXGEN_PRODUCER_H

#include "pc_gx_internal.h"
#include "acgc/gx_canonical_texgen_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert one completed setter-owned raw Texgen/SU snapshot into the
 * renderer-neutral canonical value state.  No host/OpenGL state is consulted,
 * and output is unchanged when provenance or canonical validation fails.
 */
int pc_gx_raw_texgen_build_canonical(
    const PCGXRawTexgen* input,
    AcgcGxCanonicalTexgenState* output
);

#ifdef __cplusplus
}
#endif

#endif /* PC_GX_TEXGEN_PRODUCER_H */
