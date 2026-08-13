#include "acgc/metal_packet_consumer.h"

#include <math.h>
#include <string.h>

static float float_from_bits(uint32_t bits) {
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t bits_from_float(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int build_combined_transform(
    const AcgcGxSemanticPacket* packet,
    AcgcMetalFixedTransform* transform
) {
    float projection[4][4];
    float modelview[4][4];
    float combined[4][4];
    uint32_t row;
    uint32_t column;
    uint32_t inner;

    if (packet == NULL || transform == NULL) {
        return 0;
    }

    memset(modelview, 0, sizeof(modelview));
    for (row = 0; row < 4; row++) {
        for (column = 0; column < 4; column++) {
            projection[row][column] = float_from_bits(
                packet->transform.projection[row * 4 + column]
            );
        }
    }
    for (row = 0; row < 3; row++) {
        for (column = 0; column < 4; column++) {
            modelview[row][column] = float_from_bits(
                packet->transform.modelview[row * 4 + column]
            );
        }
    }
    modelview[3][3] = 1.0f;

    for (row = 0; row < 4; row++) {
        for (column = 0; column < 4; column++) {
            float value = 0.0f;

            for (inner = 0; inner < 4; inner++) {
                value += projection[row][inner] * modelview[inner][column];
            }
            if (!isfinite(value)) {
                return 0;
            }
            combined[row][column] = value;
        }
    }

    /* The Metal fixture consumes four words per column. */
    for (column = 0; column < 4; column++) {
        for (row = 0; row < 4; row++) {
            transform->matrix[column * 4 + row] =
                bits_from_float(combined[row][column]);
        }
    }
    return 1;
}

static float clamped_unit_float(uint32_t bits) {
    float value = float_from_bits(bits);

    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static uint8_t materialize_channel(
    uint8_t vertex_channel,
    float material_channel,
    uint8_t texture_channel
) {
    float value = ((float)vertex_channel / 255.0f) *
        material_channel * ((float)texture_channel / 255.0f);

    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 1.0f) {
        return 255;
    }
    return (uint8_t)(value * 255.0f + 0.5f);
}

static uint32_t materialize_vertex_color(
    const AcgcGxSemanticPacket* packet,
    const AcgcGxSemanticVertex* vertex,
    const AcgcRendererFixtureColor* texture_color
) {
    const int use_vertex_color =
        (packet->material.flags & ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR) != 0;
    const uint8_t vertex_channels[4] = {
        use_vertex_color ? (uint8_t)(vertex->color_rgba8 >> 24) : 255,
        use_vertex_color ? (uint8_t)(vertex->color_rgba8 >> 16) : 255,
        use_vertex_color ? (uint8_t)(vertex->color_rgba8 >> 8) : 255,
        use_vertex_color ? (uint8_t)vertex->color_rgba8 : 255
    };
    const uint8_t texture_channels[4] = {
        texture_color->r,
        texture_color->g,
        texture_color->b,
        texture_color->a
    };
    uint8_t channels[4];
    uint32_t channel;

    for (channel = 0; channel < 4; channel++) {
        channels[channel] = materialize_channel(
            vertex_channels[channel],
            clamped_unit_float(packet->material.color[channel]),
            texture_channels[channel]
        );
    }
    return ((uint32_t)channels[0] << 24) |
        ((uint32_t)channels[1] << 16) |
        ((uint32_t)channels[2] << 8) |
        channels[3];
}

static AcgcMetalPacketConsumerStatus prepare_validated_packet(
    const AcgcGxSemanticPacket* packet,
    const AcgcMetalPacketConsumerTexture* texture,
    AcgcMetalPacketConsumerOutput* output,
    uint32_t semantic_version,
    uint32_t v2_extension_rendering_status
) {
    AcgcRendererFixtureColor texture_color = { 255, 255, 255, 255 };
    uint32_t vertex_index;

    if (packet->primitive != ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES ||
        packet->vertex_count != ACGC_RENDERER_GEOMETRY_MAX_VERTICES) {
        return ACGC_METAL_PACKET_CONSUMER_UNSUPPORTED_TOPOLOGY;
    }

    if ((packet->material.flags & ACGC_GX_SEMANTIC_MATERIAL_USE_TEXTURE0) != 0) {
        if (texture == NULL) {
            return ACGC_METAL_PACKET_CONSUMER_TEXTURE_REQUIRED;
        }
        if (texture->key != packet->material.texture0_key) {
            return ACGC_METAL_PACKET_CONSUMER_TEXTURE_KEY_MISMATCH;
        }
        texture_color = texture->color;
    }

    memset(output, 0, sizeof(*output));
    if (!acgc_metal_state_fixture_make(&output->state)) {
        return ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    }
    if (!build_combined_transform(packet, &output->state.transform) ||
        !acgc_metal_state_fixture_validate(&output->state)) {
        return ACGC_METAL_PACKET_CONSUMER_TRANSFORM_OVERFLOW;
    }

    output->geometry.version = ACGC_RENDERER_GEOMETRY_VERSION;
    output->geometry.vertex_count = ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
    output->geometry.draw_count = ACGC_RENDERER_GEOMETRY_MAX_DRAWS;
    for (vertex_index = 0;
         vertex_index < ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
         vertex_index++) {
        const AcgcGxSemanticVertex* source = &packet->vertices[vertex_index];
        AcgcRendererVertex* destination = &output->geometry.vertices[vertex_index];

        destination->position_x = source->position[0];
        destination->position_y = source->position[1];
        destination->position_z = source->position[2];
        destination->color_rgba8 = materialize_vertex_color(
            packet,
            source,
            &texture_color
        );
    }
    output->geometry.draws[0].primitive = ACGC_RENDERER_PRIMITIVE_TRIANGLES;
    output->geometry.draws[0].first_vertex = 0;
    output->geometry.draws[0].vertex_count = ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
    if (!acgc_renderer_geometry_validate(&output->geometry)) {
        return ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    }

    output->texture0_color = texture_color;
    output->material_flags = packet->material.flags;
    output->texture0_key = packet->material.texture0_key;
    output->semantic_version = semantic_version;
    output->v2_extension_rendering_status = v2_extension_rendering_status;
    output->v3_extension_rendering_status = 0;
    return ACGC_METAL_PACKET_CONSUMER_OK;
}

AcgcMetalPacketConsumerStatus acgc_metal_packet_consumer_prepare(
    const AcgcGxSemanticPacket* packet,
    const AcgcMetalPacketConsumerTexture* texture,
    AcgcMetalPacketConsumerOutput* output
) {
    if (packet == NULL || output == NULL) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    }
    if (!acgc_gx_semantic_packet_validate(packet)) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET;
    }
    return prepare_validated_packet(
        packet,
        texture,
        output,
        ACGC_GX_SEMANTIC_PACKET_VERSION,
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_APPLICABLE
    );
}

AcgcMetalPacketConsumerStatus acgc_metal_packet_consumer_prepare_v2(
    const AcgcGxSemanticPacketV2* packet,
    const AcgcMetalPacketConsumerTexture* texture,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalPacketConsumerStatus status;

    if (packet == NULL || output == NULL) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    }
    if (!acgc_gx_semantic_packet_v2_validate(packet)) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET;
    }

    /* The extension is validated but intentionally not interpreted here. */
    status = prepare_validated_packet(
        &packet->base,
        texture,
        output,
        ACGC_GX_SEMANTIC_PACKET_V2_VERSION,
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_RENDERED
    );
    return status;
}

AcgcMetalPacketConsumerStatus acgc_metal_packet_consumer_prepare_v3(
    const AcgcGxSemanticPacketV3* packet,
    const AcgcMetalPacketConsumerTexture* texture,
    AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalPacketConsumerStatus status;

    if (packet == NULL || output == NULL) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    }
    if (!acgc_gx_semantic_packet_v3_validate(packet)) {
        return ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET;
    }

    /* The state extension is intentionally observed but not interpreted. */
    status = prepare_validated_packet(
        &packet->base,
        texture,
        output,
        ACGC_GX_SEMANTIC_PACKET_V3_VERSION,
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_APPLICABLE
    );
    if (status == ACGC_METAL_PACKET_CONSUMER_OK) {
        output->v3_extension_rendering_status =
            ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED;
    }
    return status;
}

int acgc_metal_packet_consumer_register_runtime_callback(
    AcgcMetalPacketConsumerHandoffContext* handoff,
    AcgcMetalPacketConsumerRuntimeCallback callback,
    void* context
) {
    if (handoff == NULL || callback == NULL) {
        return 0;
    }
    handoff->runtime_callback = callback;
    handoff->runtime_callback_context = context;
    return 1;
}

void acgc_metal_packet_consumer_unregister_runtime_callback(
    AcgcMetalPacketConsumerHandoffContext* handoff
) {
    if (handoff == NULL) {
        return;
    }
    handoff->runtime_callback = NULL;
    handoff->runtime_callback_context = NULL;
}

void acgc_metal_packet_consumer_handoff(
    void* context,
    const AcgcGxSemanticPacket* packet
) {
    AcgcMetalPacketConsumerHandoffContext* handoff =
        (AcgcMetalPacketConsumerHandoffContext*)context;
    AcgcMetalPacketConsumerRuntimeCallback runtime_callback;
    void* runtime_callback_context;
    AcgcMetalPacketConsumerStatus status;

    if (handoff == NULL) {
        return;
    }
    status = acgc_metal_packet_consumer_prepare(
        packet,
        handoff->texture,
        handoff->output
    );
    handoff->status = status;

    /* Copy the borrowed pair before invoking it so the callback may unbind. */
    runtime_callback = handoff->runtime_callback;
    runtime_callback_context = handoff->runtime_callback_context;
    if (runtime_callback != NULL) {
        runtime_callback(
            runtime_callback_context,
            status == ACGC_METAL_PACKET_CONSUMER_OK ? handoff->output : NULL,
            status
        );
    }
}

void acgc_metal_packet_consumer_handoff_v2(
    void* context,
    const AcgcGxSemanticPacketV2* packet
) {
    AcgcMetalPacketConsumerHandoffContext* handoff =
        (AcgcMetalPacketConsumerHandoffContext*)context;
    AcgcMetalPacketConsumerRuntimeCallback runtime_callback;
    void* runtime_callback_context;
    AcgcMetalPacketConsumerStatus status;

    if (handoff == NULL) {
        return;
    }
    status = acgc_metal_packet_consumer_prepare_v2(
        packet,
        handoff->texture,
        handoff->output
    );
    handoff->status = status;

    /* Copy the borrowed pair before invoking it so the callback may unbind. */
    runtime_callback = handoff->runtime_callback;
    runtime_callback_context = handoff->runtime_callback_context;
    if (runtime_callback != NULL) {
        runtime_callback(
            runtime_callback_context,
            status == ACGC_METAL_PACKET_CONSUMER_OK ? handoff->output : NULL,
            status
        );
    }
}

void acgc_metal_packet_consumer_handoff_v3(
    void* context,
    const AcgcGxSemanticPacketV3* packet
) {
    AcgcMetalPacketConsumerHandoffContext* handoff =
        (AcgcMetalPacketConsumerHandoffContext*)context;
    AcgcMetalPacketConsumerRuntimeCallback runtime_callback;
    void* runtime_callback_context;
    AcgcMetalPacketConsumerStatus status;

    if (handoff == NULL) {
        return;
    }
    status = acgc_metal_packet_consumer_prepare_v3(
        packet,
        handoff->texture,
        handoff->output
    );
    handoff->status = status;

    /* Copy the borrowed pair before invoking it so the callback may unbind. */
    runtime_callback = handoff->runtime_callback;
    runtime_callback_context = handoff->runtime_callback_context;
    if (runtime_callback != NULL) {
        runtime_callback(
            runtime_callback_context,
            status == ACGC_METAL_PACKET_CONSUMER_OK ? handoff->output : NULL,
            status
        );
    }
}

const char* acgc_metal_packet_consumer_status_string(
    AcgcMetalPacketConsumerStatus status
) {
    switch (status) {
        case ACGC_METAL_PACKET_CONSUMER_OK: return "ok";
        case ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT:
            return "invalid argument";
        case ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET:
            return "invalid GX semantic packet";
        case ACGC_METAL_PACKET_CONSUMER_UNSUPPORTED_TOPOLOGY:
            return "unsupported topology";
        case ACGC_METAL_PACKET_CONSUMER_TEXTURE_REQUIRED:
            return "texture fixture required";
        case ACGC_METAL_PACKET_CONSUMER_TEXTURE_KEY_MISMATCH:
            return "texture key mismatch";
        case ACGC_METAL_PACKET_CONSUMER_TRANSFORM_OVERFLOW:
            return "transform overflow";
        case ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID:
            return "invalid consumer output";
    }
    return "unknown consumer status";
}
