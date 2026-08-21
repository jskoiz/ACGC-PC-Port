#ifndef PC_GX_BLEND_PRODUCER_H
#define PC_GX_BLEND_PRODUCER_H

#include "pc_gx_internal.h"
#include "acgc/gx_canonical_blend_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert one completed setter-owned raw Blend snapshot into the existing
 * value-only canonical Blend ABI. The producer owns neither buffer and never
 * consults the effective host/OpenGL fields in PCGXState.
 *
 * The destination is unchanged on every failure, including null pointers,
 * unknown or sticky-invalid provenance, nonzero reserved bytes, invalid GX
 * domains, and canonical validation failure.
 */
int pc_gx_raw_blend_build_canonical(
    const PCGXRawBlend* input,
    AcgcGxCanonicalBlendState* output
);

#ifdef __cplusplus
}
#endif

#endif /* PC_GX_BLEND_PRODUCER_H */
