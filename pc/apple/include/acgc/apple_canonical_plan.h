#ifndef ACGC_APPLE_CANONICAL_PLAN_H
#define ACGC_APPLE_CANONICAL_PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/apple_canonical_envelope_parser.h"
#include "acgc/gx_canonical_alpha_state.h"
#include "acgc/gx_canonical_blend_state.h"
#include "acgc/gx_canonical_channel_state.h"
#include "acgc/gx_canonical_depth_state.h"
#include "acgc/gx_canonical_dynamic_state.h"
#include "acgc/gx_canonical_geometry_state.h"
#include "acgc/gx_canonical_indirect_state.h"
#include "acgc/gx_canonical_lighting_state.h"
#include "acgc/gx_canonical_raster_state.h"
#include "acgc/gx_canonical_state.h"
#include "acgc/gx_canonical_tev_state.h"
#include "acgc/gx_canonical_texgen_state.h"
#include "acgc/gx_canonical_texture_state.h"
#include "acgc/gx_canonical_transform_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The plan is a caller-owned, fixed-size, value-only CPU handoff. It is not
 * a wire format and it contains no pointer, byte span, offset, resource
 * payload, lease, borrow, callback, or renderer object. Consumers must treat
 * a successful value as immutable and expose it through a const reference.
 */
#define ACGC_APPLE_CANONICAL_PLAN_MAX_VERTEX_COUNT \
    ACGC_GX_CANONICAL_GEOMETRY_MAX_VERTEX_COUNT

/* Normalized per-vertex component bits. Matrix selectors remain values in the
 * vertex record and their source presence remains in present_mask. */
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_POSITION (UINT32_C(1) << 0)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_NORMAL (UINT32_C(1) << 1)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_BINORMAL (UINT32_C(1) << 2)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TANGENT (UINT32_C(1) << 3)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_COLOR0 (UINT32_C(1) << 4)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_COLOR1 (UINT32_C(1) << 5)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD0 (UINT32_C(1) << 6)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD1 (UINT32_C(1) << 7)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD2 (UINT32_C(1) << 8)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD3 (UINT32_C(1) << 9)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD4 (UINT32_C(1) << 10)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD5 (UINT32_C(1) << 11)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD6 (UINT32_C(1) << 12)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD7 (UINT32_C(1) << 13)
#define ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD_MASK \
    (UINT32_C(0xFF) << 6)

typedef enum AcgcAppleCanonicalPlanStatus {
    ACGC_APPLE_CANONICAL_PLAN_OK = 0,
    ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT,
    ACGC_APPLE_CANONICAL_PLAN_INPUT_OUTPUT_OVERLAP,
    ACGC_APPLE_CANONICAL_PLAN_STRUCTURAL,
    ACGC_APPLE_CANONICAL_PLAN_SECTION_DECODE,
    ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC,
    ACGC_APPLE_CANONICAL_PLAN_DEPENDENCY,
    ACGC_APPLE_CANONICAL_PLAN_RESOURCE_METADATA,
    ACGC_APPLE_CANONICAL_PLAN_UNSUPPORTED_BUMP,
    ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT,
    ACGC_APPLE_CANONICAL_PLAN_OVERFLOW
} AcgcAppleCanonicalPlanStatus;

typedef struct AcgcAppleCanonicalPlanVertex {
    /* The original canonical attribute bits represented by this vertex. */
    uint32_t present_mask;
    /* Normalized position/normal/NBT/color/texture component bits. */
    uint32_t component_mask;
    /* Effective logical GX matrix selectors, never host addresses. */
    uint32_t position_matrix_id;
    uint32_t texture_matrix_id[8];
    /* All scalar values are canonical binary32 bit patterns. */
    uint32_t position[3];
    uint32_t normal[3];
    uint32_t binormal[3];
    uint32_t tangent[3];
    /* Logical RGBA8 packing is R in bits 0..7 through A in bits 24..31. */
    uint32_t color_rgba8[2];
    uint32_t texcoord[8][2];
} AcgcAppleCanonicalPlanVertex;

typedef struct AcgcAppleCanonicalPlanGeometry {
    uint32_t primitive;
    uint32_t vtxfmt;
    uint32_t vertex_count;
    uint32_t present_mask;
    uint32_t component_mask;
    AcgcAppleCanonicalPlanVertex vertices[
        ACGC_APPLE_CANONICAL_PLAN_MAX_VERTEX_COUNT];
} AcgcAppleCanonicalPlanGeometry;

/*
 * Every member is a value-owned canonical state or normalized Geometry. The
 * function writes this object only after all fourteen sections and all
 * cross-section checks have succeeded; every failure leaves it unchanged.
 */
typedef struct AcgcAppleCanonicalPlan {
    AcgcAppleCanonicalPlanGeometry geometry;
    AcgcGxCanonicalTransformState transform;
    AcgcGxCanonicalChannelState channels;
    AcgcGxCanonicalTexgenState texgens;
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalTevState tev;
    AcgcGxCanonicalLightingState lighting;
    AcgcGxCanonicalBlendState blend;
    AcgcGxCanonicalAlphaState alpha;
    AcgcGxCanonicalDepthState depth;
    AcgcGxCanonicalRasterState raster;
    AcgcGxCanonicalFogState fog;
    AcgcGxCanonicalIndirectState indirect;
    AcgcGxCanonicalDynamicState dynamic;
} AcgcAppleCanonicalPlan;

AcgcAppleCanonicalPlanStatus acgc_apple_canonical_plan_build(
    const uint8_t* envelope_bytes,
    size_t envelope_byte_size,
    AcgcAppleCanonicalPlan* output
);

const char* acgc_apple_canonical_plan_status_string(
    AcgcAppleCanonicalPlanStatus status
);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_APPLE_CANONICAL_PLAN_H */
