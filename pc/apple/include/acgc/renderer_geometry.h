#ifndef ACGC_RENDERER_GEOMETRY_H
#define ACGC_RENDERER_GEOMETRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Renderer-neutral packet words. Position fields are IEEE-754 binary32 bits
 * and color_rgba8 is packed as 0xRRGGBBAA. No host object or native pointer
 * is part of this contract; an Apple backend owns all Metal resources.
 */
#define ACGC_RENDERER_GEOMETRY_VERSION UINT32_C(1)
#define ACGC_RENDERER_GEOMETRY_MAX_VERTICES UINT32_C(3)
#define ACGC_RENDERER_GEOMETRY_MAX_DRAWS UINT32_C(1)

typedef enum AcgcRendererPrimitive {
    ACGC_RENDERER_PRIMITIVE_TRIANGLES = 3
} AcgcRendererPrimitive;

typedef struct AcgcRendererVertex {
    uint32_t position_x;
    uint32_t position_y;
    uint32_t position_z;
    uint32_t color_rgba8;
} AcgcRendererVertex;

typedef struct AcgcRendererDraw {
    uint32_t primitive;
    uint32_t first_vertex;
    uint32_t vertex_count;
} AcgcRendererDraw;

typedef struct AcgcRendererGeometryPacket {
    uint32_t version;
    uint32_t vertex_count;
    uint32_t draw_count;
    uint32_t reserved;
    AcgcRendererVertex vertices[ACGC_RENDERER_GEOMETRY_MAX_VERTICES];
    AcgcRendererDraw draws[ACGC_RENDERER_GEOMETRY_MAX_DRAWS];
} AcgcRendererGeometryPacket;

#if defined(__cplusplus)
#define ACGC_RENDERER_STATIC_ASSERT static_assert
#else
#define ACGC_RENDERER_STATIC_ASSERT _Static_assert
#endif

ACGC_RENDERER_STATIC_ASSERT(sizeof(AcgcRendererVertex) == 16, "renderer vertex ABI changed");
ACGC_RENDERER_STATIC_ASSERT(sizeof(AcgcRendererDraw) == 12, "renderer draw ABI changed");
ACGC_RENDERER_STATIC_ASSERT(sizeof(AcgcRendererGeometryPacket) == 76, "renderer packet ABI changed");

#undef ACGC_RENDERER_STATIC_ASSERT

/* Build the deterministic non-degenerate triangle used by the Apple fixture. */
int acgc_renderer_geometry_make_triangle(AcgcRendererGeometryPacket* packet);

/* Validate packet bounds and the renderer-neutral primitive contract. */
int acgc_renderer_geometry_validate(const AcgcRendererGeometryPacket* packet);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_RENDERER_GEOMETRY_H */
