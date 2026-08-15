#ifndef PC_GX_TRANSFORM_PRODUCER_H
#define PC_GX_TRANSFORM_PRODUCER_H

#include "pc_gx_internal.h"
#include "acgc/gx_canonical_transform_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert one borrowed, setter-owned raw Transform snapshot into the
 * renderer-neutral value state.  The input is read only for the duration of
 * the call and no pointer is retained.  The output is caller-owned and is
 * unchanged when conversion or validation fails.
 */
int pc_gx_raw_transform_build_canonical(
    const PCGXRawTransform* input,
    AcgcGxCanonicalTransformState* output
);

#ifdef __cplusplus
}
#endif

#endif /* PC_GX_TRANSFORM_PRODUCER_H */
