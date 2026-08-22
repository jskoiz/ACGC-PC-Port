#ifndef PC_GX_GEOMETRY_DEPENDENCIES_H
#define PC_GX_GEOMETRY_DEPENDENCIES_H

#include "pc_gx_internal.h"
#include "acgc/gx_canonical_channel_state.h"
#include "acgc/gx_canonical_geometry_state.h"
#include "acgc/gx_canonical_lighting_state.h"
#include "acgc/gx_canonical_texgen_state.h"
#include "acgc/gx_canonical_transform_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Derive one immutable Geometry dependency result from validated canonical
 * state and one completed, pointer-free raw Geometry batch.  The builder
 * stages the result in a local value and leaves output unchanged on every
 * failure.  It does not retain any input pointer and does not change the
 * existing Geometry serializer or any GX state.
 *
 * required_geometry_present_mask starts with the accepted raw batch's
 * present attributes.  For every emitted TEXn attribute, the active Texgen
 * source is also required to be present in that batch; a missing source is a
 * failure rather than an inferred default.  Channel and Lighting masks are
 * derived only from enabled canonical channel controls: a disabled control
 * demands neither a vertex color nor a light; an enabled control using either
 * VTX source demands its channel color slot; every light bit on an enabled
 * control is demanded.  This is the complete two-channel policy at this
 * boundary; no demand is inferred from an inactive/REG-only control or from a
 * host default.  A used BUMP generator is rejected because this boundary has
 * no canonical Bump/Indirect state from which to derive bump_known_mask.
 * texgen_present_mask is the exact validated active Texgen prefix,
 * independent of raw TEXn presence; every active coordinate receives its
 * exact ordinary logical selector and every inactive selector remains zero.
 * Therefore an active BUMP generator fails closed even when no same-index raw
 * TEXn attribute is emitted.
 */
int pc_gx_geometry_build_dependency_results(
    const PCGXRawGeometryBatch* batch,
    const AcgcGxCanonicalTransformState* transform,
    const AcgcGxCanonicalTexgenState* texgens,
    const AcgcGxCanonicalChannelState* channels,
    const AcgcGxCanonicalLightingState* lighting,
    AcgcGxCanonicalGeometryDependencyResults* output
);

#ifdef __cplusplus
}
#endif

#endif /* PC_GX_GEOMETRY_DEPENDENCIES_H */
