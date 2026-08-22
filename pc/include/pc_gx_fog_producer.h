#ifndef PC_GX_FOG_PRODUCER_H
#define PC_GX_FOG_PRODUCER_H

#include "pc_gx_internal.h"
#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert one completed setter-owned raw Fog snapshot into the existing
 * value-only canonical Fog ABI. The producer owns neither buffer and never
 * consults the effective host/OpenGL fields in PCGXState or a caller table.
 *
 * The destination is unchanged on every failure, including null pointers,
 * unknown or sticky-invalid provenance, malformed reserved words, invalid
 * GX domains, and canonical validation failure.
 */
int pc_gx_raw_fog_build_canonical(
    const PCGXRawFog* input,
    AcgcGxCanonicalFogState* output
);

#ifdef __cplusplus
}
#endif

#endif /* PC_GX_FOG_PRODUCER_H */
