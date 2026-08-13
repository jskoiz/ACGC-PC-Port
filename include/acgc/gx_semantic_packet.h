#ifndef ACGC_GX_SEMANTIC_PACKET_H
#define ACGC_GX_SEMANTIC_PACKET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A value-only packet for one bounded decoded GX draw run. Every scalar is
 * four bytes wide; the binary32 fields contain IEEE-754 float bit patterns.
 * The packet contains no native pointer, API object, or variable-length field.
 *
 * The 128-vertex bound matches emu64's decoded vertex window. A later owner of
 * pc_gx_draw_pending must split a larger deferred run into multiple packets;
 * this contract does not change the existing Windows/OpenGL submission path.
 */
#define ACGC_GX_SEMANTIC_PACKET_VERSION UINT32_C(1)
#define ACGC_GX_SEMANTIC_MAX_VERTICES UINT32_C(128)
#define ACGC_GX_SEMANTIC_PACKET_SIZE UINT32_C(4800)

/* These are semantic topology IDs, not SDL, OpenGL, or Dolphin enum values. */
#define ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES UINT32_C(1)
#define ACGC_GX_SEMANTIC_PRIMITIVE_QUADS UINT32_C(2)

/* Material flags deliberately stop at the compact v1 boundary. */
#define ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR UINT32_C(1)
#define ACGC_GX_SEMANTIC_MATERIAL_USE_TEXTURE0 UINT32_C(2)
#define ACGC_GX_SEMANTIC_MATERIAL_SUPPORTED_FLAGS \
    (ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR | \
     ACGC_GX_SEMANTIC_MATERIAL_USE_TEXTURE0)

typedef struct AcgcGxSemanticVertex {
    uint32_t position[3];
    uint32_t normal[3];
    uint32_t color_rgba8;
    uint32_t texcoord0[2];
} AcgcGxSemanticVertex;

typedef struct AcgcGxSemanticTransform {
    /* Logical row-major 4x4, 3x4, and 3x3 matrices respectively. */
    uint32_t projection[16];
    uint32_t modelview[12];
    uint32_t normal[9];
} AcgcGxSemanticTransform;

typedef struct AcgcGxSemanticMaterial {
    /* Material color is four binary32 bit patterns, not a host float array. */
    uint32_t color[4];
    /* Stable renderer-owned key; zero means no texture. Never a native pointer. */
    uint32_t texture0_key;
    uint32_t flags;
} AcgcGxSemanticMaterial;

typedef struct AcgcGxSemanticPacket {
    uint32_t version;
    uint32_t byte_size;
    uint32_t primitive;
    uint32_t vertex_count;
    uint32_t reserved;
    AcgcGxSemanticTransform transform;
    AcgcGxSemanticMaterial material;
    AcgcGxSemanticVertex vertices[ACGC_GX_SEMANTIC_MAX_VERTICES];
} AcgcGxSemanticPacket;

/*
 * GX v2 extends the v1 value packet by embedding the unchanged v1 geometry
 * prefix and appending a bounded description of the host-resolved GX state.
 * The prefix is deliberately retained so an existing v1 consumer can inspect
 * the version and reject v2 without reading the extension.
 */
#define ACGC_GX_SEMANTIC_PACKET_V2_VERSION UINT32_C(2)
#define ACGC_GX_SEMANTIC_MAX_CHANNELS UINT32_C(2)
#define ACGC_GX_SEMANTIC_MAX_TEXTURE_GENERATORS UINT32_C(2)
#define ACGC_GX_SEMANTIC_MAX_TEV_STAGES UINT32_C(2)
#define ACGC_GX_SEMANTIC_PACKET_V2_SIZE UINT32_C(5352)

#define ACGC_GX_SEMANTIC_V2_STATE_CHANNELS_KNOWN UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_STATE_TEXTURE_GENERATORS_KNOWN UINT32_C(2)
#define ACGC_GX_SEMANTIC_V2_STATE_TEXTURES_KNOWN UINT32_C(4)
#define ACGC_GX_SEMANTIC_V2_STATE_TEV_KNOWN UINT32_C(8)
#define ACGC_GX_SEMANTIC_V2_STATE_LIGHTING_KNOWN UINT32_C(16)
#define ACGC_GX_SEMANTIC_V2_STATE_FOG_KNOWN UINT32_C(32)
#define ACGC_GX_SEMANTIC_V2_STATE_INDIRECT_KNOWN UINT32_C(64)
#define ACGC_GX_SEMANTIC_V2_STATE_ALPHA_KNOWN UINT32_C(128)
#define ACGC_GX_SEMANTIC_V2_STATE_DYNAMIC_KNOWN UINT32_C(256)
#define ACGC_GX_SEMANTIC_V2_STATE_SUPPORTED UINT32_C(0x1FF)

#define ACGC_GX_SEMANTIC_V2_INDEX_NONE UINT32_C(0xFFFFFFFF)

#define ACGC_GX_SEMANTIC_V2_PROJECTION_PERSPECTIVE UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_PROJECTION_ORTHOGRAPHIC UINT32_C(2)

#define ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_REGISTER UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_VERTEX UINT32_C(2)
#define ACGC_GX_SEMANTIC_V2_CHANNEL_DIFFUSE_NONE UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_CHANNEL_ATTENUATION_NONE UINT32_C(1)

#define ACGC_GX_SEMANTIC_V2_TEXGEN_FUNCTION_MTX2X4 UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_TEXGEN_SOURCE_TEX0 UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_TEXGEN_MATRIX_IDENTITY UINT32_C(1)

/* The values mirror the stable GameCube texture-format IDs. */
#define ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_I4 UINT32_C(0)
#define ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_I8 UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_IA4 UINT32_C(2)
#define ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_IA8 UINT32_C(3)
#define ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_RGB565 UINT32_C(4)
#define ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_RGB5A3 UINT32_C(5)
#define ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_RGBA8 UINT32_C(6)
#define ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_CMPR UINT32_C(14)
#define ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C4 UINT32_C(8)
#define ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C8 UINT32_C(9)
#define ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C14X2 UINT32_C(10)

#define ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ZERO UINT32_C(0)
#define ACGC_GX_SEMANTIC_V2_COLOR_INPUT_PREVIOUS UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_COLOR_INPUT_REGISTER0 UINT32_C(2)
#define ACGC_GX_SEMANTIC_V2_COLOR_INPUT_REGISTER1 UINT32_C(3)
#define ACGC_GX_SEMANTIC_V2_COLOR_INPUT_REGISTER2 UINT32_C(4)
#define ACGC_GX_SEMANTIC_V2_COLOR_INPUT_TEXTURE UINT32_C(5)
#define ACGC_GX_SEMANTIC_V2_COLOR_INPUT_RASTER UINT32_C(6)
#define ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ONE UINT32_C(7)
#define ACGC_GX_SEMANTIC_V2_COLOR_INPUT_HALF UINT32_C(8)
#define ACGC_GX_SEMANTIC_V2_COLOR_INPUT_CONSTANT UINT32_C(9)

#define ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_ZERO UINT32_C(0)
#define ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_PREVIOUS UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_REGISTER0 UINT32_C(2)
#define ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_REGISTER1 UINT32_C(3)
#define ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_REGISTER2 UINT32_C(4)
#define ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_TEXTURE UINT32_C(5)
#define ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_RASTER UINT32_C(6)
#define ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_CONSTANT UINT32_C(7)

#define ACGC_GX_SEMANTIC_V2_TEV_OP_ADD UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_TEV_OP_SUBTRACT UINT32_C(2)
#define ACGC_GX_SEMANTIC_V2_TEV_BIAS_ZERO UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_TEV_SCALE_ONE UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_TEV_OUTPUT_PREVIOUS UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_TEV_KCOLOR_ONE UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_TEV_KCOLOR_ONE_QUARTER UINT32_C(2)
#define ACGC_GX_SEMANTIC_V2_TEV_KALPHA_ONE UINT32_C(1)
#define ACGC_GX_SEMANTIC_V2_TEV_KALPHA_ONE_QUARTER UINT32_C(2)

typedef struct AcgcGxSemanticV2Channel {
    uint32_t enabled;
    uint32_t ambient_source;
    uint32_t material_source;
    uint32_t light_mask;
    uint32_t diffuse_function;
    uint32_t attenuation_function;
    uint32_t ambient_color[4];
    uint32_t material_color[4];
} AcgcGxSemanticV2Channel;

typedef struct AcgcGxSemanticV2TextureGenerator {
    uint32_t enabled;
    uint32_t coordinate_index;
    uint32_t function;
    uint32_t source;
    uint32_t matrix;
    /* Resolved host keys. These are never guest addresses or native pointers. */
    uint32_t texture_key;
    uint32_t tlut_key;
    uint32_t sampler_key;
    uint32_t width;
    uint32_t height;
    uint32_t format;
} AcgcGxSemanticV2TextureGenerator;

typedef struct AcgcGxSemanticV2TevStage {
    uint32_t color_input[4];
    uint32_t alpha_input[4];
    uint32_t color_operation;
    uint32_t alpha_operation;
    uint32_t color_bias;
    uint32_t alpha_bias;
    uint32_t color_scale;
    uint32_t alpha_scale;
    uint32_t color_clamp;
    uint32_t alpha_clamp;
    uint32_t color_output;
    uint32_t alpha_output;
    uint32_t texture_coordinate_index;
    uint32_t texture_index;
    uint32_t raster_channel_index;
    uint32_t constant_color_selector;
    uint32_t constant_alpha_selector;
    uint32_t raster_swap;
    uint32_t texture_swap;
    uint32_t reserved;
} AcgcGxSemanticV2TevStage;

typedef struct AcgcGxSemanticPacketV2 {
    AcgcGxSemanticPacket base;
    uint32_t state_mask;
    uint32_t projection_type;
    uint32_t channel_count;
    uint32_t texture_generator_count;
    uint32_t tev_stage_count;
    uint32_t reserved[3];
    uint32_t tev_register_colors[3][4];
    uint32_t tev_swap_tables[4][4];
    AcgcGxSemanticV2Channel channels[ACGC_GX_SEMANTIC_MAX_CHANNELS];
    AcgcGxSemanticV2TextureGenerator texture_generators[
        ACGC_GX_SEMANTIC_MAX_TEXTURE_GENERATORS];
    AcgcGxSemanticV2TevStage tev_stages[ACGC_GX_SEMANTIC_MAX_TEV_STAGES];
} AcgcGxSemanticPacketV2;

#if defined(__cplusplus)
#define ACGC_GX_SEMANTIC_STATIC_ASSERT static_assert
#else
#define ACGC_GX_SEMANTIC_STATIC_ASSERT _Static_assert
#endif

ACGC_GX_SEMANTIC_STATIC_ASSERT(
    sizeof(AcgcGxSemanticVertex) == 36,
    "GX semantic vertex ABI changed"
);
ACGC_GX_SEMANTIC_STATIC_ASSERT(
    sizeof(AcgcGxSemanticTransform) == 148,
    "GX semantic transform ABI changed"
);
ACGC_GX_SEMANTIC_STATIC_ASSERT(
    sizeof(AcgcGxSemanticMaterial) == 24,
    "GX semantic material ABI changed"
);
ACGC_GX_SEMANTIC_STATIC_ASSERT(
    sizeof(AcgcGxSemanticPacket) == ACGC_GX_SEMANTIC_PACKET_SIZE,
    "GX semantic packet ABI changed"
);
ACGC_GX_SEMANTIC_STATIC_ASSERT(
    sizeof(AcgcGxSemanticV2Channel) == 56,
    "GX semantic v2 channel ABI changed"
);
ACGC_GX_SEMANTIC_STATIC_ASSERT(
    sizeof(AcgcGxSemanticV2TextureGenerator) == 44,
    "GX semantic v2 texture-generator ABI changed"
);
ACGC_GX_SEMANTIC_STATIC_ASSERT(
    sizeof(AcgcGxSemanticV2TevStage) == 104,
    "GX semantic v2 TEV ABI changed"
);
ACGC_GX_SEMANTIC_STATIC_ASSERT(
    sizeof(AcgcGxSemanticPacketV2) == ACGC_GX_SEMANTIC_PACKET_V2_SIZE,
    "GX semantic v2 packet ABI changed"
);

#undef ACGC_GX_SEMANTIC_STATIC_ASSERT

/* Initialize the fixed header, identity transforms, and white material. */
int acgc_gx_semantic_packet_init(AcgcGxSemanticPacket* packet);

/* Validate the complete value packet; malformed input is rejected. */
int acgc_gx_semantic_packet_validate(const AcgcGxSemanticPacket* packet);

/* Initialize and validate the bounded GX v2 extension. */
int acgc_gx_semantic_packet_v2_init(AcgcGxSemanticPacketV2* packet);
int acgc_gx_semantic_packet_v2_validate(const AcgcGxSemanticPacketV2* packet);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_GX_SEMANTIC_PACKET_H */
