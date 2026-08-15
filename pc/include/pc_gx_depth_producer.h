#ifndef PC_GX_DEPTH_PRODUCER_H
#define PC_GX_DEPTH_PRODUCER_H

#include "pc_gx_internal.h"
#include "acgc/gx_canonical_depth_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert one completed setter-owned raw Depth snapshot into the existing
 * value-only canonical Depth ABI.  The producer owns neither buffer and
 * never consults the effective host/OpenGL fields in PCGXState.
 *
 * The destination is unchanged on every failure, including null pointers,
 * unknown or malformed provenance, invalid GX domains, and canonical
 * validation failure.
 */
int pc_gx_raw_depth_build_canonical(
    const PCGXRawDepth* input,
    AcgcGxCanonicalDepthState* output
);

#ifdef __cplusplus
}
#endif

#endif /* PC_GX_DEPTH_PRODUCER_H */
