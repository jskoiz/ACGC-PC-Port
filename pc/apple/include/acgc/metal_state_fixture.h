#ifndef ACGC_METAL_STATE_FIXTURE_H
#define ACGC_METAL_STATE_FIXTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is a renderer-fixture contract, not a game-frame or GX packet
 * contract. Floating-point values are carried as IEEE-754 binary32 words so
 * the Apple encoder test does not depend on a host ABI's float layout.
 *
 * AcgcMetalFixedTransform is column-major: four words per column, matching
 * the order consumed by a Metal float4x4 constructor.
 */
#define ACGC_METAL_STATE_FIXTURE_VERSION UINT32_C(1)
#define ACGC_METAL_FIXED_TRANSFORM_WORDS UINT32_C(16)

#define ACGC_METAL_FLOAT_ZERO UINT32_C(0x00000000)
#define ACGC_METAL_FLOAT_ONE UINT32_C(0x3F800000)
#define ACGC_METAL_FLOAT_NEGATIVE_QUARTER UINT32_C(0xBE800000)
#define ACGC_METAL_FLOAT_EIGHTH UINT32_C(0x3E000000)
#define ACGC_METAL_FLOAT_SIXTY_FOUR UINT32_C(0x42800000)

typedef struct AcgcMetalFixedTransform {
    uint32_t matrix[ACGC_METAL_FIXED_TRANSFORM_WORDS];
} AcgcMetalFixedTransform;

typedef struct AcgcMetalFixedViewport {
    uint32_t origin_x;
    uint32_t origin_y;
    uint32_t width;
    uint32_t height;
    uint32_t znear;
    uint32_t zfar;
} AcgcMetalFixedViewport;

typedef enum AcgcMetalDepthCompareFunction {
    ACGC_METAL_DEPTH_NEVER = 0,
    ACGC_METAL_DEPTH_LESS = 1,
    ACGC_METAL_DEPTH_EQUAL = 2,
    ACGC_METAL_DEPTH_LESS_EQUAL = 3,
    ACGC_METAL_DEPTH_GREATER = 4,
    ACGC_METAL_DEPTH_NOT_EQUAL = 5,
    ACGC_METAL_DEPTH_GREATER_EQUAL = 6,
    ACGC_METAL_DEPTH_ALWAYS = 7
} AcgcMetalDepthCompareFunction;

typedef struct AcgcMetalDepthState {
    uint32_t compare_function;
    uint32_t write_enabled;
} AcgcMetalDepthState;

typedef enum AcgcMetalBlendFactor {
    ACGC_METAL_BLEND_ZERO = 0,
    ACGC_METAL_BLEND_ONE = 1,
    ACGC_METAL_BLEND_SOURCE_ALPHA = 2,
    ACGC_METAL_BLEND_ONE_MINUS_SOURCE_ALPHA = 3
} AcgcMetalBlendFactor;

typedef enum AcgcMetalBlendOperation {
    ACGC_METAL_BLEND_ADD = 0
} AcgcMetalBlendOperation;

typedef struct AcgcMetalBlendState {
    uint32_t enabled;
    uint32_t source_rgb_factor;
    uint32_t destination_rgb_factor;
    uint32_t source_alpha_factor;
    uint32_t destination_alpha_factor;
    uint32_t rgb_operation;
    uint32_t alpha_operation;
} AcgcMetalBlendState;

typedef enum AcgcMetalCullMode {
    ACGC_METAL_CULL_NONE = 0,
    ACGC_METAL_CULL_FRONT = 1,
    ACGC_METAL_CULL_BACK = 2
} AcgcMetalCullMode;

typedef enum AcgcMetalFrontFacingWinding {
    ACGC_METAL_WINDING_CLOCKWISE = 0,
    ACGC_METAL_WINDING_COUNTER_CLOCKWISE = 1
} AcgcMetalFrontFacingWinding;

typedef enum AcgcMetalTriangleFillMode {
    ACGC_METAL_TRIANGLE_FILL = 0,
    ACGC_METAL_TRIANGLE_LINES = 1
} AcgcMetalTriangleFillMode;

typedef struct AcgcMetalRasterState {
    uint32_t cull_mode;
    uint32_t front_facing_winding;
    uint32_t triangle_fill_mode;
} AcgcMetalRasterState;

typedef struct AcgcMetalStateFixture {
    uint32_t version;
    uint32_t reserved;
    AcgcMetalFixedTransform transform;
    AcgcMetalFixedViewport viewport;
    AcgcMetalDepthState depth;
    AcgcMetalBlendState blend;
    AcgcMetalRasterState raster;
} AcgcMetalStateFixture;

#if defined(__cplusplus)
#define ACGC_METAL_STATE_STATIC_ASSERT static_assert
#else
#define ACGC_METAL_STATE_STATIC_ASSERT _Static_assert
#endif

ACGC_METAL_STATE_STATIC_ASSERT(
    sizeof(AcgcMetalFixedTransform) == 64,
    "fixed transform ABI changed"
);
ACGC_METAL_STATE_STATIC_ASSERT(
    sizeof(AcgcMetalFixedViewport) == 24,
    "fixed viewport ABI changed"
);
ACGC_METAL_STATE_STATIC_ASSERT(
    sizeof(AcgcMetalDepthState) == 8,
    "depth state ABI changed"
);
ACGC_METAL_STATE_STATIC_ASSERT(
    sizeof(AcgcMetalBlendState) == 28,
    "blend state ABI changed"
);
ACGC_METAL_STATE_STATIC_ASSERT(
    sizeof(AcgcMetalRasterState) == 12,
    "raster state ABI changed"
);
ACGC_METAL_STATE_STATIC_ASSERT(
    sizeof(AcgcMetalStateFixture) == 144,
    "Metal state fixture ABI changed"
);

#undef ACGC_METAL_STATE_STATIC_ASSERT

/* Build the deterministic transform/viewport/depth/blend/raster fixture. */
int acgc_metal_state_fixture_make(AcgcMetalStateFixture* fixture);

/* Validate fixed-width state values before they reach a Metal encoder. */
int acgc_metal_state_fixture_validate(const AcgcMetalStateFixture* fixture);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_METAL_STATE_FIXTURE_H */
