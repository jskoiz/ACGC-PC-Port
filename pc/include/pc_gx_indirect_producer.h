#ifndef PC_GX_INDIRECT_PRODUCER_H
#define PC_GX_INDIRECT_PRODUCER_H

#include "pc_gx_internal.h"
#include "acgc/gx_canonical_indirect_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert one setter-owned raw Indirect snapshot into the existing
 * renderer-neutral value-only canonical Indirect ABI.  The producer reads
 * only the raw order/count/matrix subset, owns neither buffer, retains no
 * pointer, and leaves the destination unchanged on every failure.
 *
 * Per-TEV Indirect fields remain owned by the canonical TEV state.  Texture,
 * Texgen, Geometry, and TEV cross-section dependencies are validated by the
 * later cumulative assembler.
 */
int pc_gx_raw_indirect_build_canonical(
    const PCGXRawTevIndirect* input,
    AcgcGxCanonicalIndirectState* output
);

#ifdef __cplusplus
}
#endif

#endif /* PC_GX_INDIRECT_PRODUCER_H */
