#include "acgc/gx_semantic_packet.h"
#include "acgc/metal_packet_consumer.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

_Static_assert(
    sizeof(AcgcGxSemanticPacketV3) == ACGC_GX_SEMANTIC_PACKET_V3_SIZE,
    "v3 packet ABI must remain fixed-width"
);
_Static_assert(
    sizeof(AcgcGxSemanticPacketV4) == ACGC_GX_SEMANTIC_PACKET_V4_SIZE,
    "v4 packet ABI must remain fixed-width"
);
_Static_assert(
    offsetof(AcgcGxSemanticPacketV4, alpha_update_enable) ==
        ACGC_GX_SEMANTIC_PACKET_V3_SIZE,
    "v4 alpha state must follow the unchanged v3 wire layout"
);

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct ConsumerProbe {
    unsigned int calls;
    AcgcMetalPacketConsumerStatus status;
    uint32_t semantic_version;
    uint32_t alpha_write_enabled;
    int has_output;
    int geometry_valid;
} ConsumerProbe;

static void record_consumer_output(
    void* context,
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
) {
    ConsumerProbe* probe = (ConsumerProbe*)context;

    if (probe == NULL) {
        return;
    }
    probe->calls++;
    probe->status = status;
    probe->has_output = output != NULL;
    probe->geometry_valid = output != NULL &&
        acgc_renderer_geometry_validate(&output->geometry);
    if (output != NULL) {
        probe->semantic_version = output->semantic_version;
        probe->alpha_write_enabled = output->alpha_write_enabled;
    }
}

static void fill_base(AcgcGxSemanticPacket* packet) {
    static const uint32_t positions[3][3] = {
        { UINT32_C(0x00000000), UINT32_C(0x3F400000), UINT32_C(0x00000000) },
        { UINT32_C(0xBF400000), UINT32_C(0xBF266666), UINT32_C(0x00000000) },
        { UINT32_C(0x3F400000), UINT32_C(0xBF266666), UINT32_C(0x00000000) }
    };
    static const uint32_t colors[3] = {
        UINT32_C(0xF94144FF),
        UINT32_C(0x43AA8BFF),
        UINT32_C(0x577590FF)
    };
    uint32_t vertex_index;
    uint32_t component;

    packet->primitive = ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES;
    packet->vertex_count = 3;
    packet->material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;
    for (vertex_index = 0; vertex_index < 3; vertex_index++) {
        for (component = 0; component < 3; component++) {
            packet->vertices[vertex_index].position[component] =
                positions[vertex_index][component];
        }
        packet->vertices[vertex_index].color_rgba8 = colors[vertex_index];
    }
}

static void fill_texture_matrix(AcgcGxSemanticV3TextureMatrix* matrix) {
    static const uint32_t identity[12] = {
        UINT32_C(0x3F800000), UINT32_C(0x00000000), UINT32_C(0x00000000),
        UINT32_C(0x00000000), UINT32_C(0x3F800000), UINT32_C(0x00000000),
        UINT32_C(0x00000000), UINT32_C(0x00000000), UINT32_C(0x3F800000),
        UINT32_C(0x00000000), UINT32_C(0x00000000), UINT32_C(0x00000000)
    };
    uint32_t index;

    matrix->generator_index = 0;
    matrix->matrix_slot = ACGC_GX_SEMANTIC_V3_MATRIX_SLOT_NONE;
    matrix->normalize = 0;
    matrix->post_matrix = ACGC_GX_SEMANTIC_V3_POST_MATRIX_IDENTITY;
    for (index = 0; index < 12; index++) {
        matrix->matrix[index] = identity[index];
    }
}

static void fill_blend_state(AcgcGxSemanticV3BlendState* blend) {
    blend->mode = ACGC_GX_SEMANTIC_V3_BLEND_MODE_BLEND;
    blend->source_factor = ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_ALPHA;
    blend->destination_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_ALPHA;
    blend->logic_op = ACGC_GX_SEMANTIC_V3_LOGIC_NOOP;
}

static int build_v3_packet(AcgcGxSemanticPacketV3* packet) {
    if (!acgc_gx_semantic_packet_v3_init(packet)) {
        return 0;
    }
    fill_base(&packet->base);
    packet->texture_matrix_count = 1;
    fill_texture_matrix(&packet->texture_matrices[0]);
    fill_blend_state(&packet->blend);
    return acgc_gx_semantic_packet_v3_validate(packet);
}

static int build_v4_packet(
    AcgcGxSemanticPacketV4* packet,
    uint32_t alpha_update_enable
) {
    if (!acgc_gx_semantic_packet_v4_init(packet)) {
        return 0;
    }
    fill_base(&packet->base);
    packet->texture_matrix_count = 1;
    fill_texture_matrix(&packet->texture_matrices[0]);
    fill_blend_state(&packet->blend);
    packet->alpha_update_enable = alpha_update_enable;
    return acgc_gx_semantic_packet_v4_validate(packet);
}

static void reset_handoff(
    AcgcMetalPacketConsumerHandoffContext* handoff,
    AcgcMetalPacketConsumerOutput* output
) {
    handoff->texture = NULL;
    handoff->output = output;
    handoff->status = ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    handoff->runtime_callback = NULL;
    handoff->runtime_callback_context = NULL;
}

int main(void) {
    AcgcGxSemanticPacketV3 v3_packet;
    AcgcGxSemanticPacketV4 v4_packet;
    AcgcGxSemanticPacketV4 invalid_packet;
    AcgcMetalPacketConsumerHandoffContext handoff;
    AcgcMetalPacketConsumerOutput output;
    ConsumerProbe probe = { 0 };

    reset_handoff(&handoff, &output);
    CHECK(acgc_metal_packet_consumer_register_runtime_callback(
        &handoff,
        record_consumer_output,
        &probe
    ) == 1);

    CHECK(build_v3_packet(&v3_packet) == 1);
    CHECK(v3_packet.version == ACGC_GX_SEMANTIC_PACKET_V3_VERSION);
    CHECK(v3_packet.byte_size == ACGC_GX_SEMANTIC_PACKET_V3_SIZE);
    acgc_metal_packet_consumer_handoff_v3(&handoff, &v3_packet);
    CHECK(probe.calls == 1);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(probe.has_output == 1);
    CHECK(probe.geometry_valid == 1);
    CHECK(probe.semantic_version == ACGC_GX_SEMANTIC_PACKET_V3_VERSION);
    CHECK(probe.alpha_write_enabled == 1);
    CHECK(output.v3_extension_rendering_status ==
          ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED);
    CHECK(output.v4_extension_rendering_status == 0);

    CHECK(build_v4_packet(
        &v4_packet,
        ACGC_GX_SEMANTIC_V4_ALPHA_UPDATE_ENABLED
    ) == 1);
    CHECK(v4_packet.version == ACGC_GX_SEMANTIC_PACKET_V4_VERSION);
    CHECK(v4_packet.byte_size == ACGC_GX_SEMANTIC_PACKET_V4_SIZE);
    CHECK(acgc_gx_semantic_packet_v4_validate(&v4_packet) == 1);
    acgc_metal_packet_consumer_handoff_v4(&handoff, &v4_packet);
    CHECK(probe.calls == 2);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(probe.has_output == 1);
    CHECK(probe.geometry_valid == 1);
    CHECK(probe.semantic_version == ACGC_GX_SEMANTIC_PACKET_V4_VERSION);
    CHECK(probe.alpha_write_enabled == 1);
    CHECK(output.state.blend.enabled == 1);
    CHECK(output.state.blend.source_rgb_factor == ACGC_METAL_BLEND_SOURCE_ALPHA);
    CHECK(output.state.blend.destination_rgb_factor ==
          ACGC_METAL_BLEND_ONE_MINUS_SOURCE_ALPHA);
    CHECK(output.v3_extension_rendering_status ==
          ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED);
    CHECK(output.v4_extension_rendering_status == 0);

    CHECK(build_v4_packet(
        &v4_packet,
        ACGC_GX_SEMANTIC_V4_ALPHA_UPDATE_DISABLED
    ) == 1);
    acgc_metal_packet_consumer_handoff_v4(&handoff, &v4_packet);
    CHECK(probe.calls == 3);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(probe.has_output == 1);
    CHECK(probe.alpha_write_enabled == 0);
    CHECK(output.alpha_write_enabled == 0);

    /* V4 rejects a V3-shaped header; the typed V3 handoff above remains valid. */
    invalid_packet = v4_packet;
    invalid_packet.version = ACGC_GX_SEMANTIC_PACKET_V3_VERSION;
    invalid_packet.byte_size = ACGC_GX_SEMANTIC_PACKET_V3_SIZE;
    CHECK(acgc_gx_semantic_packet_v4_validate(&invalid_packet) == 0);
    acgc_metal_packet_consumer_handoff_v4(&handoff, &invalid_packet);
    CHECK(probe.calls == 4);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET);
    CHECK(probe.has_output == 0);

    /* Alpha values outside the typed V4 enum fail before the consumer boundary. */
    invalid_packet = v4_packet;
    invalid_packet.alpha_update_enable = 2;
    CHECK(acgc_gx_semantic_packet_v4_validate(&invalid_packet) == 0);
    acgc_metal_packet_consumer_handoff_v4(&handoff, &invalid_packet);
    CHECK(probe.calls == 5);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET);
    CHECK(probe.has_output == 0);

    /* V3's exact state mask cannot be presented as a V4 packet. */
    invalid_packet = v4_packet;
    invalid_packet.state_mask = ACGC_GX_SEMANTIC_PACKET_V3_STATE_SUPPORTED;
    CHECK(acgc_gx_semantic_packet_v4_validate(&invalid_packet) == 0);
    acgc_metal_packet_consumer_handoff_v4(&handoff, &invalid_packet);
    CHECK(probe.calls == 6);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET);
    CHECK(probe.has_output == 0);

    /* A packet-valid GX logic blend mode remains fail-closed at the Apple map. */
    invalid_packet = v4_packet;
    invalid_packet.blend.mode = ACGC_GX_SEMANTIC_V3_BLEND_MODE_LOGIC;
    CHECK(acgc_gx_semantic_packet_v4_validate(&invalid_packet) == 1);
    acgc_metal_packet_consumer_handoff_v4(&handoff, &invalid_packet);
    CHECK(probe.calls == 7);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET);
    CHECK(probe.has_output == 0);

    acgc_metal_packet_consumer_unregister_runtime_callback(&handoff);
    puts("pc GX V4 synthetic handoff fixture: PASS (typed V3/V4 separation, alpha validation, and fail-closed state mapping)");
    puts("proof boundary: synthetic CPU packet/consumer preparation only; no live builder, Metal, device, pixel, or playability proof");
    return 0;
}
