#ifndef PC_GX_TEV_PRODUCER_H
#define PC_GX_TEV_PRODUCER_H

#include "pc_gx_internal.h"
#include "acgc/gx_canonical_tev_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Build one canonical TEV value state from setter-owned raw provenance.
 * Neither buffer is owned by the producer, and the producer never consults
 * the legacy host mirrors or the raw Indirect-only records.
 *
 * The destination is unchanged on every failure, including incomplete raw
 * knownness, malformed values, sticky invalidity, and null pointers.
 */
int pc_gx_raw_tev_build_canonical(
    const PCGXRawTevIndirect* input,
    AcgcGxCanonicalTevState* output
);

#ifdef __cplusplus
}
#endif

#endif /* PC_GX_TEV_PRODUCER_H */
