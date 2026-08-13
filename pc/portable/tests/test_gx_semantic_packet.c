#include "acgc/gx_semantic_packet.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static uint32_t bits_from_float(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void set_vertex(
    AcgcGxSemanticVertex* vertex,
    float x,
    float y,
    float z,
    uint32_t color,
    float s,
    float t
) {
    memset(vertex, 0, sizeof(*vertex));
    vertex->position[0] = bits_from_float(x);
    vertex->position[1] = bits_from_float(y);
    vertex->position[2] = bits_from_float(z);
    vertex->normal[1] = bits_from_float(1.0f);
    vertex->color_rgba8 = color;
    vertex->texcoord0[0] = bits_from_float(s);
    vertex->texcoord0[1] = bits_from_float(t);
}

static int make_valid_triangle(AcgcGxSemanticPacket* packet) {
    if (!acgc_gx_semantic_packet_init(packet)) {
        return 0;
    }
    packet->vertex_count = 3;
    packet->material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;
    set_vertex(&packet->vertices[0], 0.0f, 0.75f, 0.0f, UINT32_C(0xF94144FF), 0.5f, 0.0f);
    set_vertex(&packet->vertices[1], -0.75f, -0.65f, 0.0f, UINT32_C(0x43AA8BFF), 0.0f, 1.0f);
    set_vertex(&packet->vertices[2], 0.75f, -0.65f, 0.0f, UINT32_C(0x577590FF), 1.0f, 1.0f);
    return acgc_gx_semantic_packet_validate(packet);
}

int main(void) {
    AcgcGxSemanticPacket packet;

    CHECK(!acgc_gx_semantic_packet_init(NULL));
    CHECK(!acgc_gx_semantic_packet_validate(NULL));
    CHECK(make_valid_triangle(&packet));
    CHECK(packet.byte_size == ACGC_GX_SEMANTIC_PACKET_SIZE);
    CHECK(packet.transform.projection[0] == bits_from_float(1.0f));
    CHECK(packet.transform.modelview[5] == bits_from_float(1.0f));
    CHECK(packet.transform.normal[8] == bits_from_float(1.0f));
    CHECK(packet.material.texture0_key == 0);

    packet.version = 0;
    CHECK(!acgc_gx_semantic_packet_validate(&packet));
    packet.version = ACGC_GX_SEMANTIC_PACKET_VERSION;
    packet.byte_size = ACGC_GX_SEMANTIC_PACKET_SIZE - 4;
    CHECK(!acgc_gx_semantic_packet_validate(&packet));
    packet.byte_size = ACGC_GX_SEMANTIC_PACKET_SIZE;
    packet.reserved = 1;
    CHECK(!acgc_gx_semantic_packet_validate(&packet));
    packet.reserved = 0;

    packet.primitive = 0;
    CHECK(!acgc_gx_semantic_packet_validate(&packet));
    packet.primitive = ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES;
    packet.vertex_count = 4;
    CHECK(!acgc_gx_semantic_packet_validate(&packet));
    packet.vertex_count = 3;
    packet.vertices[3].position[0] = bits_from_float(1.0f);
    CHECK(!acgc_gx_semantic_packet_validate(&packet));
    packet.vertices[3].position[0] = 0;

    packet.vertices[1].position[0] = UINT32_C(0x7FC00000);
    CHECK(!acgc_gx_semantic_packet_validate(&packet));
    packet.vertices[1].position[0] = bits_from_float(-0.75f);
    packet.transform.projection[0] = UINT32_C(0x7F800000);
    CHECK(!acgc_gx_semantic_packet_validate(&packet));
    packet.transform.projection[0] = bits_from_float(1.0f);
    packet.material.flags |= UINT32_C(8);
    CHECK(!acgc_gx_semantic_packet_validate(&packet));
    packet.material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;
    packet.material.texture0_key = 17;
    CHECK(!acgc_gx_semantic_packet_validate(&packet));
    packet.material.flags |= ACGC_GX_SEMANTIC_MATERIAL_USE_TEXTURE0;
    CHECK(acgc_gx_semantic_packet_validate(&packet));
    packet.material.texture0_key = 0;
    CHECK(!acgc_gx_semantic_packet_validate(&packet));

    CHECK(acgc_gx_semantic_packet_init(&packet));
    packet.primitive = ACGC_GX_SEMANTIC_PRIMITIVE_QUADS;
    packet.vertex_count = 4;
    CHECK(acgc_gx_semantic_packet_validate(&packet));
    packet.vertex_count = ACGC_GX_SEMANTIC_MAX_VERTICES + 1;
    CHECK(!acgc_gx_semantic_packet_validate(&packet));

    puts("GX semantic packet tests: PASS (fixed-width geometry/transform/material contract)");
    return 0;
}
