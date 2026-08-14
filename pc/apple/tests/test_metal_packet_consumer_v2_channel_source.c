#include "acgc/metal_packet_consumer.h"

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

static void set_valid_stage(AcgcGxSemanticV2TevStage* stage) {
    memset(stage, 0, sizeof(*stage));
    stage->color_input[0] = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ZERO;
    stage->color_input[1] = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ZERO;
    stage->color_input[2] = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ZERO;
    stage->color_input[3] = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_RASTER;
    stage->alpha_input[0] = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_ZERO;
    stage->alpha_input[1] = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_ZERO;
    stage->alpha_input[2] = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_ZERO;
    stage->alpha_input[3] = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_RASTER;
    stage->color_operation = ACGC_GX_SEMANTIC_V2_TEV_OP_ADD;
    stage->alpha_operation = ACGC_GX_SEMANTIC_V2_TEV_OP_ADD;
    stage->color_bias = ACGC_GX_SEMANTIC_V2_TEV_BIAS_ZERO;
    stage->alpha_bias = ACGC_GX_SEMANTIC_V2_TEV_BIAS_ZERO;
    stage->color_scale = ACGC_GX_SEMANTIC_V2_TEV_SCALE_ONE;
    stage->alpha_scale = ACGC_GX_SEMANTIC_V2_TEV_SCALE_ONE;
    stage->color_clamp = 1;
    stage->alpha_clamp = 1;
    stage->color_output = ACGC_GX_SEMANTIC_V2_TEV_OUTPUT_PREVIOUS;
    stage->alpha_output = ACGC_GX_SEMANTIC_V2_TEV_OUTPUT_PREVIOUS;
    stage->texture_coordinate_index = 0;
    stage->texture_index = 0;
    stage->raster_channel_index = 0;
    stage->constant_color_selector = ACGC_GX_SEMANTIC_V2_TEV_KCOLOR_ONE;
    stage->constant_alpha_selector = ACGC_GX_SEMANTIC_V2_TEV_KALPHA_ONE;
}

static int make_packet(AcgcGxSemanticPacketV2* packet) {
    uint32_t vertex_index;

    if (!acgc_gx_semantic_packet_v2_init(packet)) {
        return 0;
    }
    packet->base.primitive = ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES;
    packet->base.vertex_count = 3;
    packet->state_mask = ACGC_GX_SEMANTIC_V2_STATE_SUPPORTED;
    packet->projection_type = ACGC_GX_SEMANTIC_V2_PROJECTION_PERSPECTIVE;
    packet->channel_count = 1;
    packet->texture_generator_count = 1;
    packet->tev_stage_count = 1;
    packet->channels[0].ambient_source =
        ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_REGISTER;
    packet->channels[0].material_source =
        ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_VERTEX;
    packet->channels[0].diffuse_function =
        ACGC_GX_SEMANTIC_V2_CHANNEL_DIFFUSE_NONE;
    packet->channels[0].attenuation_function =
        ACGC_GX_SEMANTIC_V2_CHANNEL_ATTENUATION_NONE;
    packet->texture_generators[0].enabled = 1;
    packet->texture_generators[0].coordinate_index = 0;
    packet->texture_generators[0].function =
        ACGC_GX_SEMANTIC_V2_TEXGEN_FUNCTION_MTX2X4;
    packet->texture_generators[0].source =
        ACGC_GX_SEMANTIC_V2_TEXGEN_SOURCE_TEX0;
    packet->texture_generators[0].matrix =
        ACGC_GX_SEMANTIC_V2_TEXGEN_MATRIX_IDENTITY;
    packet->texture_generators[0].texture_key = 17;
    packet->texture_generators[0].sampler_key = 17;
    packet->texture_generators[0].width = 4;
    packet->texture_generators[0].height = 4;
    packet->texture_generators[0].format =
        ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_RGBA8;
    set_valid_stage(&packet->tev_stages[0]);
    for (vertex_index = 0; vertex_index < 3; vertex_index++) {
        packet->base.vertices[vertex_index].position[0] =
            bits_from_float((float)vertex_index);
        packet->base.vertices[vertex_index].position[1] =
            bits_from_float(0.0f);
        packet->base.vertices[vertex_index].position[2] =
            bits_from_float(0.0f);
        packet->base.vertices[vertex_index].color_rgba8 = 0x102030FF;
    }
    return 1;
}

typedef struct V2Capture {
    uint32_t calls;
    const AcgcMetalPacketConsumerOutput* output;
    AcgcMetalPacketConsumerStatus status;
} V2Capture;

static void capture_v2(
    void* context,
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
) {
    V2Capture* capture = (V2Capture*)context;

    if (capture == NULL) {
        return;
    }
    capture->calls++;
    capture->output = output;
    capture->status = status;
}

static void reset_handoff(
    AcgcMetalPacketConsumerHandoffContext* handoff,
    AcgcMetalPacketConsumerOutput* output
) {
    memset(handoff, 0, sizeof(*handoff));
    memset(output, 0xA5, sizeof(*output));
    handoff->output = output;
    handoff->status = ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
}

int main(void) {
    AcgcGxSemanticPacketV2 packet;
    AcgcGxSemanticPacketV2 invalid;
    AcgcGxSemanticPacket v1_packet;
    AcgcMetalPacketConsumerHandoffContext handoff;
    AcgcMetalPacketConsumerOutput output;
    V2Capture capture = { 0 };

    /* Disabled GX_SRC_REG/GX_SRC_VTX is a valid value-only V2 contract. */
    CHECK(make_packet(&packet));
    CHECK(acgc_gx_semantic_packet_v2_channel_source_is_valid(
              &packet.channels[0]));
    CHECK(acgc_gx_semantic_packet_v2_validate(&packet));
    CHECK(acgc_metal_packet_consumer_prepare_v2(&packet, NULL, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.semantic_version == ACGC_GX_SEMANTIC_PACKET_V2_VERSION);
    CHECK(output.material_flags == ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR);
    CHECK(output.v2_extension_rendering_status ==
          ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_RENDERED);

    reset_handoff(&handoff, &output);
    CHECK(acgc_metal_packet_consumer_register_runtime_callback(
              &handoff, capture_v2, &capture));
    acgc_metal_packet_consumer_handoff_v2(&handoff, &packet);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(capture.calls == 1 && capture.output == &output);
    CHECK(capture.status == ACGC_METAL_PACKET_CONSUMER_OK);

    /* V2 remains fail-closed for enabled or lighted vertex-source channels. */
    invalid = packet;
    invalid.channels[0].enabled = 1;
    CHECK(!acgc_gx_semantic_packet_v2_channel_source_is_valid(
              &invalid.channels[0]));
    CHECK(!acgc_gx_semantic_packet_v2_validate(&invalid));
    CHECK(acgc_metal_packet_consumer_prepare_v2(
              &invalid, NULL, &output) ==
          ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET);

    invalid = packet;
    invalid.channels[0].light_mask = 1;
    CHECK(!acgc_gx_semantic_packet_v2_validate(&invalid));

    invalid = packet;
    invalid.channels[0].ambient_source =
        ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_VERTEX;
    CHECK(!acgc_gx_semantic_packet_v2_validate(&invalid));

    invalid = packet;
    invalid.channels[0].material_source = 99;
    CHECK(!acgc_gx_semantic_packet_v2_validate(&invalid));

    /* The legacy V1 validator and consumer remain independent. */
    CHECK(acgc_gx_semantic_packet_init(&v1_packet));
    v1_packet.primitive = ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES;
    v1_packet.vertex_count = 3;
    CHECK(acgc_gx_semantic_packet_validate(&v1_packet));
    CHECK(acgc_metal_packet_consumer_prepare(
              &v1_packet, NULL, &output) == ACGC_METAL_PACKET_CONSUMER_OK);

    puts("Apple V2 channel-source contract fixture: PASS");
    puts("proof boundary: typed CPU packet validation/preparation only; no Metal encode, frame, pixel, device, or playability claim");
    return 0;
}
