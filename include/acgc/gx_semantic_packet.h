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

#undef ACGC_GX_SEMANTIC_STATIC_ASSERT

/* Initialize the fixed header, identity transforms, and white material. */
int acgc_gx_semantic_packet_init(AcgcGxSemanticPacket* packet);

/* Validate the complete value packet; malformed input is rejected. */
int acgc_gx_semantic_packet_validate(const AcgcGxSemanticPacket* packet);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_GX_SEMANTIC_PACKET_H */
