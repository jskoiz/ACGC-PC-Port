#ifndef PC_GX_GEOMETRY_PRODUCER_H
#define PC_GX_GEOMETRY_PRODUCER_H

#include <stddef.h>
#include <stdint.h>

#include "pc_gx_internal.h"
#include "acgc/gx_canonical_geometry_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Build one existing canonical Geometry section from one completed raw batch.
 * The caller owns both buffers.  The producer never retains either buffer,
 * never reads a borrowed GX array pointer, and never allocates.
 *
 * On failure, output and output_size are unchanged.  scratch is a staging
 * area and may be modified before a failure is reported.  The two buffers
 * and output_size must be disjoint.  The maximum section extent is the
 * existing canonical ABI limit, not a new packet or Geometry format.
 */
#define PC_GX_GEOMETRY_PRODUCER_MAX_SECTION_BYTES \
    ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE

int pc_gx_geometry_build_canonical(
    const PCGXRawGeometryBatch* batch,
    const AcgcGxCanonicalGeometryDependencyResults* dependencies,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_size,
    uint8_t* scratch,
    size_t scratch_capacity
);

#ifdef __cplusplus
}
#endif

#endif /* PC_GX_GEOMETRY_PRODUCER_H */
