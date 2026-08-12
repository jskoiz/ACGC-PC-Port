#include "acgc/renderer_geometry.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static float float_from_bits(uint32_t bits) {
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static float twice_signed_area(const AcgcRendererGeometryPacket* packet) {
    float x0 = float_from_bits(packet->vertices[0].position_x);
    float y0 = float_from_bits(packet->vertices[0].position_y);
    float x1 = float_from_bits(packet->vertices[1].position_x);
    float y1 = float_from_bits(packet->vertices[1].position_y);
    float x2 = float_from_bits(packet->vertices[2].position_x);
    float y2 = float_from_bits(packet->vertices[2].position_y);

    return (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
}

int main(void) {
    AcgcRendererGeometryPacket packet;

    CHECK(acgc_renderer_geometry_make_triangle(&packet));
    CHECK(acgc_renderer_geometry_validate(&packet));
    CHECK(packet.version == ACGC_RENDERER_GEOMETRY_VERSION);
    CHECK(packet.vertex_count == 3);
    CHECK(packet.draw_count == 1);
    CHECK(twice_signed_area(&packet) != 0.0f);
    CHECK(packet.vertices[0].color_rgba8 != packet.vertices[1].color_rgba8);
    CHECK(packet.vertices[1].color_rgba8 != packet.vertices[2].color_rgba8);
    CHECK(packet.draws[0].primitive == ACGC_RENDERER_PRIMITIVE_TRIANGLES);
    CHECK(packet.draws[0].first_vertex == 0);
    CHECK(packet.draws[0].vertex_count == 3);

    packet.reserved = 1;
    CHECK(!acgc_renderer_geometry_validate(&packet));
    packet.reserved = 0;
    packet.draws[0].vertex_count = 2;
    CHECK(!acgc_renderer_geometry_validate(&packet));
    packet.draws[0].vertex_count = 3;
    packet.draws[0].first_vertex = 1;
    CHECK(!acgc_renderer_geometry_validate(&packet));
    packet.draws[0].first_vertex = 0;
    packet.version = 0;
    CHECK(!acgc_renderer_geometry_validate(&packet));
    CHECK(!acgc_renderer_geometry_make_triangle(NULL));

    puts("renderer geometry tests: PASS (fixed-width triangle packet)");
    return 0;
}
