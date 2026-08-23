#include "acgc/renderer_geometry.h"

#include <string.h>

static int draw_is_valid(
    const AcgcRendererDraw* draw,
    uint32_t vertex_count
) {
    if (draw == NULL || draw->primitive != ACGC_RENDERER_PRIMITIVE_TRIANGLES ||
        draw->vertex_count < 3 || (draw->vertex_count % 3) != 0 ||
        draw->first_vertex > vertex_count ||
        draw->vertex_count > vertex_count - draw->first_vertex) {
        return 0;
    }
    return 1;
}

int acgc_renderer_geometry_validate(const AcgcRendererGeometryPacket* packet) {
    uint32_t draw_index;

    if (packet == NULL || packet->version != ACGC_RENDERER_GEOMETRY_VERSION ||
        packet->reserved != 0 || packet->vertex_count == 0 ||
        packet->vertex_count > ACGC_RENDERER_GEOMETRY_MAX_VERTICES ||
        packet->draw_count == 0 ||
        packet->draw_count > ACGC_RENDERER_GEOMETRY_MAX_DRAWS) {
        return 0;
    }
    for (draw_index = 0; draw_index < packet->draw_count; draw_index++) {
        if (!draw_is_valid(&packet->draws[draw_index], packet->vertex_count)) {
            return 0;
        }
    }
    return 1;
}

int acgc_renderer_geometry_make_triangle(AcgcRendererGeometryPacket* packet) {
    if (packet == NULL) {
        return 0;
    }
    memset(packet, 0, sizeof(*packet));
    packet->version = ACGC_RENDERER_GEOMETRY_VERSION;
    packet->vertex_count = ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES;
    packet->draw_count = 1;

    /* NDC positions: top, lower-left, lower-right. */
    packet->vertices[0].position_x = UINT32_C(0x00000000); /*  0.00f */
    packet->vertices[0].position_y = UINT32_C(0x3F400000); /*  0.75f */
    packet->vertices[0].position_z = UINT32_C(0x00000000); /*  0.00f */
    packet->vertices[0].color_rgba8 = UINT32_C(0xF94144FF); /* red */

    packet->vertices[1].position_x = UINT32_C(0xBF400000); /* -0.75f */
    packet->vertices[1].position_y = UINT32_C(0xBF266666); /* -0.65f */
    packet->vertices[1].position_z = UINT32_C(0x00000000); /*  0.00f */
    packet->vertices[1].color_rgba8 = UINT32_C(0x43AA8BFF); /* green */

    packet->vertices[2].position_x = UINT32_C(0x3F400000); /*  0.75f */
    packet->vertices[2].position_y = UINT32_C(0xBF266666); /* -0.65f */
    packet->vertices[2].position_z = UINT32_C(0x00000000); /*  0.00f */
    packet->vertices[2].color_rgba8 = UINT32_C(0x577590FF); /* blue */

    packet->draws[0].primitive = ACGC_RENDERER_PRIMITIVE_TRIANGLES;
    packet->draws[0].first_vertex = 0;
    packet->draws[0].vertex_count =
        ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES;
    return acgc_renderer_geometry_validate(packet);
}
