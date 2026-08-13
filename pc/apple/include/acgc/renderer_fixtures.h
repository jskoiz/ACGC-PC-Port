#ifndef ACGC_RENDERER_FIXTURES_H
#define ACGC_RENDERER_FIXTURES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dependency-free Apple renderer seam fixtures. These records contain only
 * fixed-width values; the byte buffers passed to the functions are synthetic
 * host memory and are not serialized into a renderer packet.
 *
 * This is a reference/contract fixture, not a Metal backend and not a game
 * rendering path. Mip-chain, indirect-texture, and TEV compare-op behavior
 * remain outside this bounded lane.
 */
#define ACGC_RENDERER_FIXTURE_VERSION UINT32_C(1)
#define ACGC_RENDERER_FIXTURE_MAX_TEV_STAGES UINT32_C(3)
#define ACGC_RENDERER_FIXTURE_MAX_TLUT_ENTRIES UINT32_C(16384)

typedef enum AcgcRendererFixtureTextureFormat {
    ACGC_RENDERER_FIXTURE_TF_I4 = 0x0,
    ACGC_RENDERER_FIXTURE_TF_I8 = 0x1,
    ACGC_RENDERER_FIXTURE_TF_IA4 = 0x2,
    ACGC_RENDERER_FIXTURE_TF_IA8 = 0x3,
    ACGC_RENDERER_FIXTURE_TF_RGB565 = 0x4,
    ACGC_RENDERER_FIXTURE_TF_RGB5A3 = 0x5,
    ACGC_RENDERER_FIXTURE_TF_RGBA8 = 0x6,
    ACGC_RENDERER_FIXTURE_TF_C4 = 0x8,
    ACGC_RENDERER_FIXTURE_TF_C8 = 0x9,
    ACGC_RENDERER_FIXTURE_TF_C14X2 = 0xA,
    ACGC_RENDERER_FIXTURE_TF_CMPR = 0xE
} AcgcRendererFixtureTextureFormat;

typedef enum AcgcRendererFixtureByteOrder {
    ACGC_RENDERER_FIXTURE_BIG_ENDIAN = 0,
    ACGC_RENDERER_FIXTURE_LITTLE_ENDIAN = 1
} AcgcRendererFixtureByteOrder;

typedef enum AcgcRendererFixtureTlutFormat {
    ACGC_RENDERER_FIXTURE_TL_IA8 = 0,
    ACGC_RENDERER_FIXTURE_TL_RGB565 = 1,
    ACGC_RENDERER_FIXTURE_TL_RGB5A3 = 2
} AcgcRendererFixtureTlutFormat;

typedef struct AcgcRendererFixtureTextureDescription {
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t data_byte_order;
    uint32_t data_size;
    uint32_t tlut_format;
    uint32_t tlut_entries;
    uint32_t tlut_data_size;
    uint32_t tlut_byte_order;
} AcgcRendererFixtureTextureDescription;

/* Returns the exact base-level tile-storage size, or zero for invalid input. */
uint32_t acgc_renderer_fixture_texture_bytes(
    uint32_t width,
    uint32_t height,
    uint32_t format
);

/* Decode GameCube tile storage into tightly packed RGBA8 output. */
int acgc_renderer_fixture_decode_texture(
    const AcgcRendererFixtureTextureDescription* description,
    const uint8_t* data,
    const uint8_t* tlut_data,
    uint8_t* rgba_output,
    uint32_t rgba_capacity
);

typedef enum AcgcRendererFixtureWrapMode {
    ACGC_RENDERER_FIXTURE_WRAP_CLAMP = 0,
    ACGC_RENDERER_FIXTURE_WRAP_REPEAT = 1,
    ACGC_RENDERER_FIXTURE_WRAP_MIRROR = 2
} AcgcRendererFixtureWrapMode;

typedef enum AcgcRendererFixtureFilter {
    ACGC_RENDERER_FIXTURE_FILTER_NEAREST = 0,
    ACGC_RENDERER_FIXTURE_FILTER_LINEAR = 1,
    ACGC_RENDERER_FIXTURE_FILTER_NEAR_MIP_NEAR = 2,
    ACGC_RENDERER_FIXTURE_FILTER_LINEAR_MIP_NEAR = 3,
    ACGC_RENDERER_FIXTURE_FILTER_NEAR_MIP_LINEAR = 4,
    ACGC_RENDERER_FIXTURE_FILTER_LINEAR_MIP_LINEAR = 5
} AcgcRendererFixtureFilter;

typedef struct AcgcRendererFixtureSamplerDescription {
    uint32_t version;
    uint32_t wrap_s;
    uint32_t wrap_t;
    uint32_t min_filter;
    uint32_t mag_filter;
    uint32_t filtering_enabled;
} AcgcRendererFixtureSamplerDescription;

typedef struct AcgcRendererFixtureSamplerState {
    uint32_t address_s;
    uint32_t address_t;
    uint32_t min_filter;
    uint32_t mag_filter;
} AcgcRendererFixtureSamplerState;

/* Resolve GX-style base-level sampler choices to backend-neutral modes. */
int acgc_renderer_fixture_resolve_sampler(
    const AcgcRendererFixtureSamplerDescription* description,
    AcgcRendererFixtureSamplerState* state
);

typedef struct AcgcRendererFixtureColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} AcgcRendererFixtureColor;

typedef enum AcgcRendererFixtureTevColorArg {
    ACGC_RENDERER_FIXTURE_CC_CPREV = 0,
    ACGC_RENDERER_FIXTURE_CC_APREV = 1,
    ACGC_RENDERER_FIXTURE_CC_C0 = 2,
    ACGC_RENDERER_FIXTURE_CC_A0 = 3,
    ACGC_RENDERER_FIXTURE_CC_C1 = 4,
    ACGC_RENDERER_FIXTURE_CC_A1 = 5,
    ACGC_RENDERER_FIXTURE_CC_C2 = 6,
    ACGC_RENDERER_FIXTURE_CC_A2 = 7,
    ACGC_RENDERER_FIXTURE_CC_TEXC = 8,
    ACGC_RENDERER_FIXTURE_CC_TEXA = 9,
    ACGC_RENDERER_FIXTURE_CC_RASC = 10,
    ACGC_RENDERER_FIXTURE_CC_RASA = 11,
    ACGC_RENDERER_FIXTURE_CC_ONE = 12,
    ACGC_RENDERER_FIXTURE_CC_HALF = 13,
    ACGC_RENDERER_FIXTURE_CC_KONST = 14,
    ACGC_RENDERER_FIXTURE_CC_ZERO = 15
} AcgcRendererFixtureTevColorArg;

typedef enum AcgcRendererFixtureTevAlphaArg {
    ACGC_RENDERER_FIXTURE_CA_APREV = 0,
    ACGC_RENDERER_FIXTURE_CA_A0 = 1,
    ACGC_RENDERER_FIXTURE_CA_A1 = 2,
    ACGC_RENDERER_FIXTURE_CA_A2 = 3,
    ACGC_RENDERER_FIXTURE_CA_TEXA = 4,
    ACGC_RENDERER_FIXTURE_CA_RASA = 5,
    ACGC_RENDERER_FIXTURE_CA_KONST = 6,
    ACGC_RENDERER_FIXTURE_CA_ZERO = 7
} AcgcRendererFixtureTevAlphaArg;

typedef enum AcgcRendererFixtureTevOp {
    ACGC_RENDERER_FIXTURE_TEV_ADD = 0,
    ACGC_RENDERER_FIXTURE_TEV_SUB = 1
} AcgcRendererFixtureTevOp;

typedef enum AcgcRendererFixtureTevBias {
    ACGC_RENDERER_FIXTURE_TEV_BIAS_ZERO = 0,
    ACGC_RENDERER_FIXTURE_TEV_BIAS_ADD_HALF = 1,
    ACGC_RENDERER_FIXTURE_TEV_BIAS_SUB_HALF = 2
} AcgcRendererFixtureTevBias;

typedef enum AcgcRendererFixtureTevScale {
    ACGC_RENDERER_FIXTURE_TEV_SCALE_ONE = 0,
    ACGC_RENDERER_FIXTURE_TEV_SCALE_TWO = 1,
    ACGC_RENDERER_FIXTURE_TEV_SCALE_FOUR = 2,
    ACGC_RENDERER_FIXTURE_TEV_SCALE_HALF = 3
} AcgcRendererFixtureTevScale;

typedef enum AcgcRendererFixtureTevRegister {
    ACGC_RENDERER_FIXTURE_TEV_PREV = 0,
    ACGC_RENDERER_FIXTURE_TEV_REG0 = 1,
    ACGC_RENDERER_FIXTURE_TEV_REG1 = 2,
    ACGC_RENDERER_FIXTURE_TEV_REG2 = 3
} AcgcRendererFixtureTevRegister;

typedef struct AcgcRendererFixtureTevStage {
    uint32_t color_a;
    uint32_t color_b;
    uint32_t color_c;
    uint32_t color_d;
    uint32_t alpha_a;
    uint32_t alpha_b;
    uint32_t alpha_c;
    uint32_t alpha_d;
    uint32_t color_op;
    uint32_t color_bias;
    uint32_t color_scale;
    uint32_t color_clamp;
    uint32_t color_out;
    uint32_t alpha_op;
    uint32_t alpha_bias;
    uint32_t alpha_scale;
    uint32_t alpha_clamp;
    uint32_t alpha_out;
    uint32_t konst_color_sel;
    uint32_t konst_alpha_sel;
} AcgcRendererFixtureTevStage;

typedef struct AcgcRendererFixtureTevState {
    uint32_t version;
    uint32_t stage_count;
    AcgcRendererFixtureColor prev;
    AcgcRendererFixtureColor reg0;
    AcgcRendererFixtureColor reg1;
    AcgcRendererFixtureColor reg2;
    AcgcRendererFixtureColor texture[ACGC_RENDERER_FIXTURE_MAX_TEV_STAGES];
    AcgcRendererFixtureColor raster;
    AcgcRendererFixtureColor konst[4];
    AcgcRendererFixtureTevStage stages[ACGC_RENDERER_FIXTURE_MAX_TEV_STAGES];
} AcgcRendererFixtureTevState;

/* Evaluate add/sub TEV stages using deterministic signed Q8.8 arithmetic. */
int acgc_renderer_fixture_tev_evaluate(
    const AcgcRendererFixtureTevState* state,
    AcgcRendererFixtureColor* output
);

#if defined(__cplusplus)
#define ACGC_RENDERER_FIXTURE_STATIC_ASSERT static_assert
#else
#define ACGC_RENDERER_FIXTURE_STATIC_ASSERT _Static_assert
#endif

ACGC_RENDERER_FIXTURE_STATIC_ASSERT(
    sizeof(AcgcRendererFixtureColor) == 4,
    "renderer fixture color ABI changed"
);
ACGC_RENDERER_FIXTURE_STATIC_ASSERT(
    sizeof(AcgcRendererFixtureTextureDescription) == 40,
    "renderer fixture texture description ABI changed"
);
ACGC_RENDERER_FIXTURE_STATIC_ASSERT(
    sizeof(AcgcRendererFixtureSamplerDescription) == 24,
    "renderer fixture sampler description ABI changed"
);
ACGC_RENDERER_FIXTURE_STATIC_ASSERT(
    sizeof(AcgcRendererFixtureSamplerState) == 16,
    "renderer fixture sampler state ABI changed"
);
ACGC_RENDERER_FIXTURE_STATIC_ASSERT(
    sizeof(AcgcRendererFixtureTevStage) == 80,
    "renderer fixture TEV stage ABI changed"
);
ACGC_RENDERER_FIXTURE_STATIC_ASSERT(
    sizeof(AcgcRendererFixtureTevState) == 296,
    "renderer fixture TEV state ABI changed"
);

#undef ACGC_RENDERER_FIXTURE_STATIC_ASSERT

#ifdef __cplusplus
}
#endif

#endif /* ACGC_RENDERER_FIXTURES_H */
