#include "acgc/gx_semantic_packet.h"

#include <string.h>

static int binary32_is_finite(uint32_t bits) {
    /* IEEE-754 exponent all-ones encodes both infinities and NaNs. */
    return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

static int binary32_words_are_finite(const uint32_t* words, size_t count) {
    size_t index;

    if (words == NULL) {
        return 0;
    }
    for (index = 0; index < count; index++) {
        if (!binary32_is_finite(words[index])) {
            return 0;
        }
    }
    return 1;
}

static int words_are_zero(const uint32_t* words, size_t count) {
    size_t index;

    if (words == NULL) {
        return 0;
    }
    for (index = 0; index < count; index++) {
        if (words[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int vertex_is_valid(const AcgcGxSemanticVertex* vertex) {
    if (vertex == NULL ||
        !binary32_words_are_finite(vertex->position, 3) ||
        !binary32_words_are_finite(vertex->normal, 3) ||
        !binary32_words_are_finite(vertex->texcoord0, 2)) {
        return 0;
    }
    return 1;
}

static int vertex_is_zero(const AcgcGxSemanticVertex* vertex) {
    return vertex != NULL &&
        words_are_zero(vertex->position, 3) &&
        words_are_zero(vertex->normal, 3) &&
        vertex->color_rgba8 == 0 &&
        words_are_zero(vertex->texcoord0, 2);
}

static int vertex_count_is_valid(uint32_t primitive, uint32_t vertex_count) {
    if (vertex_count == 0 || vertex_count > ACGC_GX_SEMANTIC_MAX_VERTICES) {
        return 0;
    }
    switch (primitive) {
        case ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES:
            return (vertex_count % 3) == 0;
        case ACGC_GX_SEMANTIC_PRIMITIVE_QUADS:
            return (vertex_count % 4) == 0;
        default:
            return 0;
    }
}

int acgc_gx_semantic_packet_init(AcgcGxSemanticPacket* packet) {
    if (packet == NULL) {
        return 0;
    }

    memset(packet, 0, sizeof(*packet));
    packet->version = ACGC_GX_SEMANTIC_PACKET_VERSION;
    packet->byte_size = ACGC_GX_SEMANTIC_PACKET_SIZE;
    packet->primitive = ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES;

    /* Identity projection/model-view/normal transforms and white material. */
    packet->transform.projection[0] = UINT32_C(0x3F800000);
    packet->transform.projection[5] = UINT32_C(0x3F800000);
    packet->transform.projection[10] = UINT32_C(0x3F800000);
    packet->transform.projection[15] = UINT32_C(0x3F800000);
    packet->transform.modelview[0] = UINT32_C(0x3F800000);
    packet->transform.modelview[5] = UINT32_C(0x3F800000);
    packet->transform.modelview[10] = UINT32_C(0x3F800000);
    packet->transform.normal[0] = UINT32_C(0x3F800000);
    packet->transform.normal[4] = UINT32_C(0x3F800000);
    packet->transform.normal[8] = UINT32_C(0x3F800000);
    packet->material.color[0] = UINT32_C(0x3F800000);
    packet->material.color[1] = UINT32_C(0x3F800000);
    packet->material.color[2] = UINT32_C(0x3F800000);
    packet->material.color[3] = UINT32_C(0x3F800000);
    return 1;
}

int acgc_gx_semantic_packet_validate(const AcgcGxSemanticPacket* packet) {
    uint32_t vertex_index;

    if (packet == NULL ||
        packet->version != ACGC_GX_SEMANTIC_PACKET_VERSION ||
        packet->byte_size != ACGC_GX_SEMANTIC_PACKET_SIZE ||
        packet->reserved != 0 ||
        !vertex_count_is_valid(packet->primitive, packet->vertex_count) ||
        !binary32_words_are_finite(packet->transform.projection, 16) ||
        !binary32_words_are_finite(packet->transform.modelview, 12) ||
        !binary32_words_are_finite(packet->transform.normal, 9) ||
        !binary32_words_are_finite(packet->material.color, 4) ||
        (packet->material.flags & ~ACGC_GX_SEMANTIC_MATERIAL_SUPPORTED_FLAGS) != 0) {
        return 0;
    }

    if ((packet->material.flags & ACGC_GX_SEMANTIC_MATERIAL_USE_TEXTURE0) != 0) {
        if (packet->material.texture0_key == 0) {
            return 0;
        }
    } else if (packet->material.texture0_key != 0) {
        return 0;
    }

    for (vertex_index = 0; vertex_index < packet->vertex_count; vertex_index++) {
        if (!vertex_is_valid(&packet->vertices[vertex_index])) {
            return 0;
        }
    }
    for (; vertex_index < ACGC_GX_SEMANTIC_MAX_VERTICES; vertex_index++) {
        if (!vertex_is_zero(&packet->vertices[vertex_index])) {
            return 0;
        }
    }
    return 1;
}
