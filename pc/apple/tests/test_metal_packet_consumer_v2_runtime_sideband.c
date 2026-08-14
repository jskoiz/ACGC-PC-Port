#include "acgc/metal_packet_consumer.h"

#include <stdio.h>
#include <string.h>

extern int pc_metal_runtime_sink_eligible_fixture(
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
);

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct CallbackCapture {
    uint32_t count;
    AcgcMetalPacketConsumerStatus status;
    const AcgcMetalPacketConsumerOutput* output;
} CallbackCapture;

typedef struct SourceCapture {
    AcgcMetalPacketConsumerV2TextureSource source;
    uint32_t calls;
    uint32_t mutate_generation;
    uint32_t invalid_source_kind;
} SourceCapture;

static uint32_t bits_from_float(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void callback(
    void* context,
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
) {
    CallbackCapture* capture = (CallbackCapture*)context;

    capture->count++;
    capture->status = status;
    capture->output = output;
}

static int source_provider(
    void* context,
    uint32_t map,
    AcgcMetalPacketConsumerV2TextureSource* destination
) {
    SourceCapture* capture = (SourceCapture*)context;

    if (capture == NULL || destination == NULL || map != 0) {
        return 0;
    }
    capture->calls++;
    *destination = capture->source;
    if (capture->mutate_generation && capture->calls % 2 == 0) {
        destination->generation++;
    }
    if (capture->invalid_source_kind) {
        destination->source_kind = UINT32_C(99);
    }
    return 1;
}

static void set_texture_description(
    AcgcRendererFixtureTextureDescription* description
) {
    memset(description, 0, sizeof(*description));
    description->version = ACGC_RENDERER_FIXTURE_VERSION;
    description->width = 8;
    description->height = 8;
    description->format = ACGC_RENDERER_FIXTURE_TF_C4;
    description->data_byte_order = ACGC_RENDERER_FIXTURE_BIG_ENDIAN;
    description->data_size = 32;
    description->tlut_format = ACGC_RENDERER_FIXTURE_TL_RGB565;
    description->tlut_entries = 16;
    description->tlut_data_size = 32;
    description->tlut_byte_order = ACGC_RENDERER_FIXTURE_BIG_ENDIAN;
}

static void set_valid_stage(AcgcGxSemanticV2TevStage* stage) {
    memset(stage, 0, sizeof(*stage));
    stage->color_input[0] = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ZERO;
    stage->color_input[1] = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_TEXTURE;
    stage->color_input[2] = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ONE;
    stage->color_input[3] = ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ZERO;
    stage->alpha_input[0] = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_ZERO;
    stage->alpha_input[1] = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_TEXTURE;
    stage->alpha_input[2] = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_CONSTANT;
    stage->alpha_input[3] = ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_ZERO;
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
        ACGC_GX_SEMANTIC_V2_CHANNEL_SOURCE_REGISTER;
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
    packet->texture_generators[0].texture_key = 42;
    packet->texture_generators[0].tlut_key = 42;
    packet->texture_generators[0].sampler_key = 42;
    packet->texture_generators[0].width = 8;
    packet->texture_generators[0].height = 8;
    packet->texture_generators[0].format = ACGC_GX_SEMANTIC_V2_TEXTURE_FORMAT_C4;
    set_valid_stage(&packet->tev_stages[0]);
    packet->tev_swap_tables[0][0] = 0;
    packet->tev_swap_tables[0][1] = 1;
    packet->tev_swap_tables[0][2] = 2;
    packet->tev_swap_tables[0][3] = 3;
    packet->tev_swap_tables[1][0] = 1;
    packet->tev_swap_tables[1][1] = 0;
    packet->tev_swap_tables[1][2] = 2;
    packet->tev_swap_tables[1][3] = 3;
    packet->tev_stages[0].texture_swap = 1;
    for (vertex_index = 0; vertex_index < 3; vertex_index++) {
        packet->base.vertices[vertex_index].color_rgba8 = 0x10203040;
        packet->base.vertices[vertex_index].texcoord0[0] = bits_from_float(0.0f);
        packet->base.vertices[vertex_index].texcoord0[1] = bits_from_float(0.0f);
    }
    return acgc_gx_semantic_packet_v2_validate(packet);
}

static void set_texture_fixture(
    AcgcMetalPacketConsumerV2TextureFixture* fixture,
    uint8_t* texture_data,
    uint8_t* tlut_data,
    uint8_t* decoded_rgba
) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->key = 42;
    set_texture_description(&fixture->description);
    fixture->data = texture_data;
    fixture->tlut_data = tlut_data;
    fixture->decoded_rgba = decoded_rgba;
    fixture->decoded_rgba_capacity = 8 * 8 * 4;
    fixture->sampler.version = ACGC_RENDERER_FIXTURE_VERSION;
    fixture->sampler.wrap_s = ACGC_RENDERER_FIXTURE_WRAP_CLAMP;
    fixture->sampler.wrap_t = ACGC_RENDERER_FIXTURE_WRAP_CLAMP;
    fixture->sampler.min_filter = ACGC_RENDERER_FIXTURE_FILTER_NEAREST;
    fixture->sampler.mag_filter = ACGC_RENDERER_FIXTURE_FILTER_NEAREST;
    fixture->sampler.filtering_enabled = 1;
}

static int test_sink_eligibility_policy(void) {
    AcgcMetalPacketConsumerOutput output;

    memset(&output, 0, sizeof(output));
    output.semantic_version = ACGC_GX_SEMANTIC_PACKET_VERSION;
    CHECK(pc_metal_runtime_sink_eligible_fixture(
              &output, ACGC_METAL_PACKET_CONSUMER_OK));

    /* An ordinary V2 result is a valid handoff, but its prefix is not a
     * renderable sink input while channel/texgen/TEV state is unimplemented. */
    output.semantic_version = ACGC_GX_SEMANTIC_PACKET_V2_VERSION;
    output.v2_extension_rendering_status =
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_RENDERED;
    CHECK(!pc_metal_runtime_sink_eligible_fixture(
              &output, ACGC_METAL_PACKET_CONSUMER_OK));

    /* A provider-resolved V2 extension is still CPU-only contract data. */
    output.v2_extension_rendering_status =
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_CPU_RESOLVED;
    CHECK(!pc_metal_runtime_sink_eligible_fixture(
              &output, ACGC_METAL_PACKET_CONSUMER_OK));

    /* A well-formed V3 typed handoff remains explicitly non-rendered. */
    memset(&output, 0, sizeof(output));
    output.semantic_version = ACGC_GX_SEMANTIC_PACKET_V3_VERSION;
    output.v2_extension_rendering_status =
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_APPLICABLE;
    output.v3_extension_rendering_status =
        ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED;
    CHECK(!pc_metal_runtime_sink_eligible_fixture(
              &output, ACGC_METAL_PACKET_CONSUMER_OK));

    /* V4 maps only a bounded subset and cannot reach the geometry-only sink
     * until a cumulative canonical CPU render plan exists. */
    output.semantic_version = ACGC_GX_SEMANTIC_PACKET_V4_VERSION;
    CHECK(!pc_metal_runtime_sink_eligible_fixture(
              &output, ACGC_METAL_PACKET_CONSUMER_OK));

    /* Unknown versions/status tuples, null output, and non-OK handoffs fail
     * closed instead of relying on a permissive fallback. */
    output.v2_extension_rendering_status =
        ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_APPLICABLE;
    CHECK(!pc_metal_runtime_sink_eligible_fixture(
              &output, ACGC_METAL_PACKET_CONSUMER_OK));
    output.semantic_version = UINT32_C(99);
    CHECK(!pc_metal_runtime_sink_eligible_fixture(
              &output, ACGC_METAL_PACKET_CONSUMER_OK));
    CHECK(!pc_metal_runtime_sink_eligible_fixture(
              NULL, ACGC_METAL_PACKET_CONSUMER_OK));
    output.semantic_version = ACGC_GX_SEMANTIC_PACKET_VERSION;
    CHECK(!pc_metal_runtime_sink_eligible_fixture(
              &output, ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET));
    output.v3_extension_rendering_status =
        ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED;
    CHECK(!pc_metal_runtime_sink_eligible_fixture(
              &output, ACGC_METAL_PACKET_CONSUMER_OK));

    return 0;
}

int main(void) {
    AcgcGxSemanticPacketV2 packet;
    AcgcMetalPacketConsumerOutput output;
    AcgcMetalPacketConsumerV2TextureFixture fixture;
    AcgcMetalPacketConsumerHandoffContext handoff;
    CallbackCapture capture;
    SourceCapture source_capture;
    _Alignas(32) uint8_t texture_data[32] = { 0 };
    _Alignas(32) uint8_t tlut_data[32] = { 0 };
    uint8_t decoded_rgba[8 * 8 * 4] = { 0 };

    CHECK(test_sink_eligibility_policy() == 0);

    /* C4 index zero resolves through a synthetic RGB565 red TLUT entry. */
    tlut_data[0] = 0xF8;
    CHECK(make_packet(&packet));
    set_texture_fixture(&fixture, texture_data, tlut_data, decoded_rgba);
    memset(&output, 0, sizeof(output));
    memset(&handoff, 0, sizeof(handoff));
    memset(&capture, 0, sizeof(capture));
    handoff.output = &output;
    CHECK(acgc_metal_packet_consumer_register_runtime_callback(
              &handoff, callback, &capture));

    /* A texture-using packet cannot silently fall back to geometry-only. */
    acgc_metal_packet_consumer_handoff_v2(&handoff, &packet);
    CHECK(handoff.status ==
          ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_REQUIRED);
    CHECK(capture.count == 1 && capture.output == NULL);

    /* The explicit borrowed source reaches the existing CPU resolver. */
    CHECK(acgc_metal_packet_consumer_bind_v2_texture_sideband(
              &handoff, &fixture, 1));
    acgc_metal_packet_consumer_handoff_v2(&handoff, &packet);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(capture.count == 2 && capture.output == &output);
    CHECK(output.v2_extension_rendering_status ==
          ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_CPU_RESOLVED);
    CHECK(output.texture0_color.r == 255 && output.texture0_color.g == 0 &&
          output.texture0_color.b == 0 && output.texture0_color.a == 255);

    /* A bound but malformed caller source still fails closed. */
    fixture.data = NULL;
    acgc_metal_packet_consumer_handoff_v2(&handoff, &packet);
    CHECK(handoff.status ==
          ACGC_METAL_PACKET_CONSUMER_TEXTURE_FIXTURE_INVALID);
    CHECK(capture.count == 3 && capture.output == NULL);

    /* The live PC source path fills only the borrowed fixture metadata; the
     * caller still owns the decode scratch and the consumer rechecks the
     * source generation after CPU decode. */
    memset(&source_capture, 0, sizeof(source_capture));
    source_capture.source.image_ptr = texture_data;
    source_capture.source.image_byte_size = sizeof(texture_data);
    source_capture.source.tlut_ptr = tlut_data;
    source_capture.source.tlut_byte_size = sizeof(tlut_data);
    source_capture.source.tlut_format = ACGC_RENDERER_FIXTURE_TL_RGB565;
    source_capture.source.tlut_entries = 16;
    source_capture.source.tlut_name = 0;
    source_capture.source.tlut_is_be = 1;
    source_capture.source.width = 8;
    source_capture.source.height = 8;
    source_capture.source.format = ACGC_RENDERER_FIXTURE_TF_C4;
    source_capture.source.wrap_s = ACGC_RENDERER_FIXTURE_WRAP_CLAMP;
    source_capture.source.wrap_t = ACGC_RENDERER_FIXTURE_WRAP_CLAMP;
    source_capture.source.min_filter = ACGC_RENDERER_FIXTURE_FILTER_NEAREST;
    source_capture.source.mag_filter = ACGC_RENDERER_FIXTURE_FILTER_NEAREST;
    source_capture.source.effective_filter =
        ACGC_RENDERER_FIXTURE_FILTER_NEAREST;
    source_capture.source.source_kind =
        ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_RAW_GUEST;
    source_capture.source.tlut_source_kind =
        ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_RAW_GUEST;
    source_capture.source.generation = 17;
    fixture.data = NULL;
    fixture.tlut_data = NULL;
    CHECK(acgc_metal_packet_consumer_bind_v2_texture_source_provider(
              &handoff, source_provider, &source_capture));
    acgc_metal_packet_consumer_handoff_v2(&handoff, &packet);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(capture.count == 4 && capture.output == &output);
    CHECK(output.v2_extension_rendering_status ==
          ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_CPU_RESOLVED);
    CHECK(output.texture0_color.r == 255 && output.texture0_color.g == 0 &&
          output.texture0_color.b == 0 && output.texture0_color.a == 255);
    CHECK(source_capture.calls == 2);

    source_capture.mutate_generation = 1;
    acgc_metal_packet_consumer_handoff_v2(&handoff, &packet);
    CHECK(handoff.status ==
          ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_LIFETIME_CHANGED);
    CHECK(capture.count == 5 && capture.output == NULL);

    source_capture.mutate_generation = 0;
    source_capture.invalid_source_kind = 1;
    acgc_metal_packet_consumer_handoff_v2(&handoff, &packet);
    CHECK(handoff.status ==
          ACGC_METAL_PACKET_CONSUMER_V2_TEXTURE_SOURCE_INVALID);
    CHECK(capture.count == 6 && capture.output == NULL);

    /* Clear the borrowed source, then preserve the geometry-only V2 path. */
    acgc_metal_packet_consumer_clear_v2_texture_sideband(&handoff);
    packet.tev_stages[0].color_input[1] =
        ACGC_GX_SEMANTIC_V2_COLOR_INPUT_ZERO;
    packet.tev_stages[0].alpha_input[1] =
        ACGC_GX_SEMANTIC_V2_ALPHA_INPUT_ZERO;
    CHECK(acgc_gx_semantic_packet_v2_validate(&packet));
    acgc_metal_packet_consumer_handoff_v2(&handoff, &packet);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(capture.count == 7 && capture.output == &output);
    CHECK(output.v2_extension_rendering_status ==
          ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_RENDERED);

    puts("Apple V2 runtime texture sideband fixture: PASS");
    puts("proof boundary: borrowed CPU metadata/fixture handoff only; no Metal object, draw, frame, pixel, or playability claim");
    return 0;
}
