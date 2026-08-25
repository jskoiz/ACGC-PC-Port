#import <Foundation/Foundation.h>

#include "acgc/gx_semantic_packet.h"
#include "acgc/metal_packet_consumer.h"
#include "acgc/metal_sink.h"
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

#define SKIP_NO_METAL 77

static int make_packet_output(AcgcMetalPacketConsumerOutput* output) {
    AcgcGxSemanticPacket packet;
    AcgcRendererGeometryPacket geometry;
    uint32_t vertex_index;

    if (output == NULL || !acgc_renderer_geometry_make_triangle(&geometry) ||
        !acgc_gx_semantic_packet_init(&packet)) {
        return 0;
    }

    packet.vertex_count = ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES;
    packet.material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;
    for (vertex_index = 0;
         vertex_index < ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES;
         vertex_index++) {
        memcpy(
            packet.vertices[vertex_index].position,
            &geometry.vertices[vertex_index].position_x,
            sizeof(packet.vertices[vertex_index].position)
        );
        packet.vertices[vertex_index].color_rgba8 =
            geometry.vertices[vertex_index].color_rgba8;
    }
    return acgc_metal_packet_consumer_prepare(&packet, NULL, output) ==
        ACGC_METAL_PACKET_CONSUMER_OK;
}

static uint32_t bits_from_float(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void set_mapped_canonical_raster(
    AcgcMetalPacketConsumerOutput* output
) {
    output->canonical_raster_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_MAPPED;
    memset(&output->canonical_raster, 0, sizeof(output->canonical_raster));
    output->canonical_raster.viewport_bits[0] = output->state.viewport.origin_x;
    output->canonical_raster.viewport_bits[1] = output->state.viewport.origin_y;
    output->canonical_raster.viewport_bits[2] = output->state.viewport.width;
    output->canonical_raster.viewport_bits[3] = output->state.viewport.height;
    output->canonical_raster.viewport_bits[4] = output->state.viewport.znear;
    output->canonical_raster.viewport_bits[5] = output->state.viewport.zfar;
    output->canonical_raster.scissor[2] = 64;
    output->canonical_raster.scissor[3] = 64;
    output->canonical_raster.clip_mode =
        ACGC_GX_CANONICAL_RASTER_CLIP_MODE_ENABLE;
    output->canonical_raster.cull_mode = output->state.raster.cull_mode;
}

static void set_decomp_dynamic_canonical_raster(
    AcgcMetalPacketConsumerOutput* output
) {
    output->state.viewport.width = bits_from_float(640.0f);
    output->state.viewport.height = bits_from_float(480.0f);
    set_mapped_canonical_raster(output);
    output->canonical_raster.scissor[2] = 640;
    output->canonical_raster.scissor[3] = 480;
    output->canonical_raster.line_width = 5;
    output->canonical_raster.point_size = 6;
    output->canonical_raster.dither = 1;
    output->canonical_raster.field_mode = 1;
    output->canonical_raster.half_aspect_ratio = 0;
    output->canonical_raster.field_odd_mask = 1;
    output->canonical_raster.field_even_mask = 1;
}

static void set_passthrough_canonical_alpha(
    AcgcMetalPacketConsumerOutput* output
) {
    output->canonical_alpha_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_PASSTHROUGH;
    output->canonical_alpha.comp0 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX;
    output->canonical_alpha.ref0 = 17;
    output->canonical_alpha.op = ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN;
    output->canonical_alpha.comp1 = ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX;
    output->canonical_alpha.ref1 = 29;
    output->canonical_alpha.color_update_enable =
        ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX;
    output->canonical_alpha.alpha_update_enable =
        output->alpha_write_enabled;
    output->canonical_alpha.z_comp_loc_before_tex = 1;
}

static void set_inactive_canonical_fog(
    AcgcMetalPacketConsumerOutput* output
) {
    output->canonical_fog_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_DISPOSITION_INACTIVE;
    memset(&output->canonical_fog, 0, sizeof(output->canonical_fog));
}

static int test_cpu_contract(
    AcgcMetalPacketConsumerOutput* output,
    AcgcMetalSinkStatus* init_status
) {
    AcgcMetalPacketConsumerOutput invalid_output;
    AcgcMetalPacketConsumerOutput multi_output;
    AcgcMetalSinkSnapshot before_staged;
    AcgcMetalSinkSnapshot after_staged;
    AcgcMetalSinkSnapshot snapshot;
    AcgcMetalSinkSnapshot before_dynamic;
    AcgcMetalSinkSnapshot after_dynamic;
    AcgcMetalPacketConsumerOutput dynamic_output;
    uint32_t vertex_index;

    acgc_metal_sink_get_snapshot(&snapshot);
    CHECK(snapshot.initialized == 0);
    CHECK(acgc_metal_sink_submit(NULL) == ACGC_METAL_SINK_NOT_INITIALIZED);
    CHECK(make_packet_output(output));
    CHECK(output->geometry.vertex_count ==
          ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES);
    CHECK(output->geometry.draw_count == ACGC_RENDERER_GEOMETRY_MAX_DRAWS);
    CHECK(output->geometry.draws[0].primitive ==
          ACGC_RENDERER_PRIMITIVE_TRIANGLES);
    CHECK(output->geometry.draws[0].first_vertex == 0);
    CHECK(output->geometry.draws[0].vertex_count ==
          ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES);

    *init_status = acgc_metal_sink_init();
    CHECK(*init_status == ACGC_METAL_SINK_OK ||
          *init_status == ACGC_METAL_SINK_NO_DEVICE);
    CHECK(acgc_metal_sink_init() == *init_status);
    acgc_metal_sink_get_snapshot(&snapshot);
    CHECK(snapshot.initialized == 1);
    CHECK(snapshot.available == (*init_status == ACGC_METAL_SINK_OK));

    invalid_output = *output;
    invalid_output.geometry.reserved = 1;
    CHECK(acgc_metal_sink_submit(&invalid_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&snapshot);
    CHECK(snapshot.submit_count == 1);
    CHECK(snapshot.completed_count == 0);
    CHECK(snapshot.readback_count == 0);
    CHECK(snapshot.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    multi_output = *output;
    multi_output.source_kind =
        ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN;
    multi_output.semantic_version = 0;
    multi_output.v2_extension_rendering_status = 0;
    multi_output.v3_extension_rendering_status = 0;
    multi_output.v4_extension_rendering_status = 0;
    multi_output.canonical_tev_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_STAGED_UNRENDERED;
    multi_output.canonical_blend_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_STAGED_UNRENDERED;
    multi_output.canonical_blend.mode =
        ACGC_GX_SEMANTIC_V3_BLEND_MODE_LOGIC;
    multi_output.canonical_blend.source_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_ALPHA;
    multi_output.canonical_blend.destination_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_ALPHA;
    multi_output.canonical_blend.logic_op = ACGC_GX_SEMANTIC_V3_LOGIC_XOR;
    set_passthrough_canonical_alpha(&multi_output);
    set_mapped_canonical_raster(&multi_output);
    set_inactive_canonical_fog(&multi_output);
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* Blend staging is rejected independently of the TEV disposition. */
    multi_output.canonical_tev_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_VERTEX_COLOR_PASSTHROUGH;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* The exact mapped disposition carries position-correct GX factors. */
    multi_output.canonical_blend_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_MAPPED;
    multi_output.canonical_blend.mode =
        ACGC_GX_SEMANTIC_V3_BLEND_MODE_BLEND;
    multi_output.canonical_blend.source_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_COLOR;
    multi_output.canonical_blend.destination_factor =
        ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_COLOR;
    multi_output.canonical_blend.logic_op = ACGC_GX_SEMANTIC_V3_LOGIC_CLEAR;
    multi_output.state.blend.enabled = 1;
    multi_output.state.blend.source_rgb_factor =
        ACGC_METAL_BLEND_DESTINATION_COLOR;
    multi_output.state.blend.destination_rgb_factor =
        ACGC_METAL_BLEND_ONE_MINUS_SOURCE_COLOR;
    multi_output.state.blend.source_alpha_factor =
        ACGC_METAL_BLEND_DESTINATION_COLOR;
    multi_output.state.blend.destination_alpha_factor =
        ACGC_METAL_BLEND_ONE_MINUS_SOURCE_COLOR;
    multi_output.state.blend.rgb_operation = ACGC_METAL_BLEND_ADD;
    multi_output.state.blend.alpha_operation = ACGC_METAL_BLEND_ADD;

    /* Alpha staging is rejected before any Metal allocation or encode. */
    multi_output.canonical_alpha_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_STAGED_UNRENDERED;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* A passthrough disposition with a non-tautological predicate is malformed. */
    set_passthrough_canonical_alpha(&multi_output);
    multi_output.canonical_alpha.comp0 =
        ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 1;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* The sink also checks the exact alpha-write relationship independently. */
    set_passthrough_canonical_alpha(&multi_output);
    multi_output.canonical_alpha.alpha_update_enable =
        multi_output.alpha_write_enabled ==
            ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX
        ? ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MIN
        : ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* A valid live-shaped Raster remains staged and is rejected before
     * submit/completion/readback can reach the Metal command path. */
    multi_output.canonical_tev_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_VERTEX_COLOR_PASSTHROUGH;
    multi_output.canonical_blend_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_MAPPED;
    multi_output.canonical_alpha_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_PASSTHROUGH;
    set_mapped_canonical_raster(&multi_output);
    multi_output.canonical_raster.viewport_bits[2] = bits_from_float(640.0f);
    multi_output.canonical_raster.viewport_bits[3] = bits_from_float(480.0f);
    multi_output.canonical_raster.scissor[2] = 640;
    multi_output.canonical_raster.scissor[3] = 480;
    multi_output.canonical_raster_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_STAGED_UNRENDERED;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* A mapped disposition cannot disguise a canonical/state value mismatch. */
    set_mapped_canonical_raster(&multi_output);
    multi_output.canonical_raster.viewport_bits[2] = bits_from_float(63.0f);
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* Invalid canonical words remain fail-closed even with MAPPED selected. */
    set_mapped_canonical_raster(&multi_output);
    multi_output.canonical_raster.reserved[0] = 1;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* The copied canonical cull value must correlate with materialized state. */
    set_mapped_canonical_raster(&multi_output);
    multi_output.state.raster.cull_mode = ACGC_METAL_CULL_FRONT;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);
    multi_output.state.raster.cull_mode = output->state.raster.cull_mode;
    set_mapped_canonical_raster(&multi_output);

    /* Fog staging is rejected before any Metal allocation or encoding. */
    multi_output.canonical_fog_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_FOG_DISPOSITION_STAGED_UNRENDERED;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* An active Fog value cannot be mislabeled as inactive. */
    set_inactive_canonical_fog(&multi_output);
    multi_output.canonical_fog.fog_type = ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN;
    multi_output.canonical_fog.start_bits = bits_from_float(0.25f);
    multi_output.canonical_fog.end_bits = bits_from_float(0.75f);
    multi_output.canonical_fog.near_bits = bits_from_float(0.1f);
    multi_output.canonical_fog.far_bits = bits_from_float(1.0f);
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* A copied Fog value with invalid reserved data remains fail-closed. */
    set_inactive_canonical_fog(&multi_output);
    multi_output.canonical_fog.reserved[0] = 1;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* Enabling range adjustment also makes the Fog value unrendered. */
    set_inactive_canonical_fog(&multi_output);
    multi_output.canonical_fog.range_adjust_enable = 1;
    acgc_metal_sink_get_snapshot(&before_staged);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_staged);
    CHECK(after_staged.submit_count == before_staged.submit_count + 1);
    CHECK(after_staged.completed_count == before_staged.completed_count);
    CHECK(after_staged.readback_count == before_staged.readback_count);
    CHECK(after_staged.last_status == ACGC_METAL_SINK_INVALID_OUTPUT);

    /* The exact passthrough disposition retains the existing sink contract. */
    set_inactive_canonical_fog(&multi_output);
    set_passthrough_canonical_alpha(&multi_output);
    multi_output.canonical_tev_disposition =
        ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_VERTEX_COLOR_PASSTHROUGH;

    /* The exact decomp-shaped 640x480 Raster maps independently of the old
     * 64x64 fixture path and remains bounded through submit/readback. */
    dynamic_output = multi_output;
    set_decomp_dynamic_canonical_raster(&dynamic_output);
    CHECK(dynamic_output.canonical_raster_disposition ==
          ACGC_METAL_PACKET_CONSUMER_CANONICAL_RASTER_DISPOSITION_MAPPED);
    CHECK(dynamic_output.state.viewport.width == bits_from_float(640.0f));
    CHECK(dynamic_output.state.viewport.height == bits_from_float(480.0f));
    CHECK(dynamic_output.canonical_raster.scissor[2] == 640);
    CHECK(dynamic_output.canonical_raster.scissor[3] == 480);
    dynamic_output.state.viewport.width = bits_from_float(1706.0f);
    dynamic_output.canonical_raster.viewport_bits[2] =
        bits_from_float(1706.0f);
    dynamic_output.canonical_raster.scissor[2] = 1705;
    acgc_metal_sink_get_snapshot(&before_dynamic);
    CHECK(acgc_metal_sink_submit(&dynamic_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_dynamic);
    CHECK(after_dynamic.submit_count == before_dynamic.submit_count + 1);
    CHECK(after_dynamic.completed_count == before_dynamic.completed_count);
    CHECK(after_dynamic.readback_count == before_dynamic.readback_count);
    set_decomp_dynamic_canonical_raster(&dynamic_output);
    dynamic_output.geometry.draws[0].primitive = 0;
    acgc_metal_sink_get_snapshot(&before_dynamic);
    CHECK(acgc_metal_sink_submit(&dynamic_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_dynamic);
    CHECK(after_dynamic.submit_count == before_dynamic.submit_count + 1);
    CHECK(after_dynamic.completed_count == before_dynamic.completed_count);
    CHECK(after_dynamic.readback_count == before_dynamic.readback_count);
    dynamic_output.geometry.draws[0].primitive =
        multi_output.geometry.draws[0].primitive;
    set_decomp_dynamic_canonical_raster(&dynamic_output);
    dynamic_output.canonical_raster.scissor[2] = 639;
    acgc_metal_sink_get_snapshot(&before_dynamic);
    CHECK(acgc_metal_sink_submit(&dynamic_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_dynamic);
    CHECK(after_dynamic.submit_count == before_dynamic.submit_count + 1);
    CHECK(after_dynamic.completed_count == before_dynamic.completed_count);
    CHECK(after_dynamic.readback_count == before_dynamic.readback_count);
    set_decomp_dynamic_canonical_raster(&dynamic_output);
    dynamic_output.canonical_raster.clip_mode =
        ACGC_GX_CANONICAL_RASTER_CLIP_MODE_DISABLE;
    acgc_metal_sink_get_snapshot(&before_dynamic);
    CHECK(acgc_metal_sink_submit(&dynamic_output) ==
          ACGC_METAL_SINK_INVALID_OUTPUT);
    acgc_metal_sink_get_snapshot(&after_dynamic);
    CHECK(after_dynamic.submit_count == before_dynamic.submit_count + 1);
    CHECK(after_dynamic.completed_count == before_dynamic.completed_count);
    CHECK(after_dynamic.readback_count == before_dynamic.readback_count);
    set_decomp_dynamic_canonical_raster(&dynamic_output);
    acgc_metal_sink_get_snapshot(&before_dynamic);
    CHECK(acgc_metal_sink_submit(&dynamic_output) ==
          (*init_status == ACGC_METAL_SINK_OK
              ? ACGC_METAL_SINK_OK
              : ACGC_METAL_SINK_NO_DEVICE));
    acgc_metal_sink_get_snapshot(&after_dynamic);
    CHECK(after_dynamic.submit_count == before_dynamic.submit_count + 1);
    if (*init_status == ACGC_METAL_SINK_OK) {
        CHECK(after_dynamic.completed_count ==
              before_dynamic.completed_count + 1);
        CHECK(after_dynamic.readback_count ==
              before_dynamic.readback_count + 1);
        CHECK(after_dynamic.last_checksum != 0);
        CHECK((after_dynamic.last_pixel_rgba8 & UINT32_C(0xFF)) ==
              UINT32_C(0xFF));
    } else {
        CHECK(after_dynamic.completed_count == before_dynamic.completed_count);
        CHECK(after_dynamic.readback_count == before_dynamic.readback_count);
    }

    for (vertex_index = 0;
         vertex_index < ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
         vertex_index++) {
        multi_output.geometry.vertices[vertex_index] =
            output->geometry.vertices[vertex_index %
                ACGC_RENDERER_GEOMETRY_LEGACY_TRIANGLE_VERTICES];
    }
    multi_output.geometry.vertex_count = ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
    multi_output.geometry.draws[0].vertex_count =
        ACGC_RENDERER_GEOMETRY_MAX_VERTICES;
    CHECK(acgc_renderer_geometry_validate(&multi_output.geometry));
    CHECK(multi_output.geometry.draw_count == ACGC_RENDERER_GEOMETRY_MAX_DRAWS);
    CHECK(multi_output.geometry.draws[0].vertex_count ==
          ACGC_RENDERER_GEOMETRY_MAX_VERTICES);
    CHECK(acgc_metal_sink_submit(&multi_output) ==
          (*init_status == ACGC_METAL_SINK_OK
              ? ACGC_METAL_SINK_OK
              : ACGC_METAL_SINK_NO_DEVICE));
    return 0;
}

int main(void) {
    AcgcMetalPacketConsumerOutput output;
    AcgcMetalSinkSnapshot first;
    AcgcMetalSinkSnapshot second;
    AcgcMetalSinkSnapshot after_shutdown;
    AcgcMetalSinkStatus init_status;

    @autoreleasepool {
        CHECK(test_cpu_contract(&output, &init_status) == 0);
        if (init_status == ACGC_METAL_SINK_NO_DEVICE) {
            CHECK(acgc_metal_sink_submit(&output) ==
                  ACGC_METAL_SINK_NO_DEVICE);
            acgc_metal_sink_get_snapshot(&first);
            CHECK(first.available == 0);
            CHECK(first.completed_count == 0);
            CHECK(first.readback_count == 0);
            CHECK(first.last_status == ACGC_METAL_SINK_NO_DEVICE);
            acgc_metal_sink_shutdown();
            acgc_metal_sink_shutdown();
            puts("Metal sink: CPU contract PASS; SKIP (no macOS Metal device available)");
            return SKIP_NO_METAL;
        }

        CHECK(acgc_metal_sink_submit(&output) == ACGC_METAL_SINK_OK);
        acgc_metal_sink_get_snapshot(&first);
        CHECK(first.submit_count == 21);
        CHECK(first.completed_count == 3);
        CHECK(first.readback_count == 3);
        CHECK(first.last_status == ACGC_METAL_SINK_OK);
        CHECK(first.last_pixel_rgba8 != UINT32_C(0x000000FF));
        CHECK((first.last_pixel_rgba8 & UINT32_C(0xFF)) == UINT32_C(0xFF));
        CHECK(first.last_checksum != 0);

        /* A second synchronous pass must produce the same bounded readback. */
        CHECK(acgc_metal_sink_submit(&output) == ACGC_METAL_SINK_OK);
        acgc_metal_sink_get_snapshot(&second);
        CHECK(second.submit_count == 22);
        CHECK(second.completed_count == 4);
        CHECK(second.readback_count == 4);
        CHECK(second.last_status == ACGC_METAL_SINK_OK);
        CHECK(second.last_pixel_rgba8 == first.last_pixel_rgba8);
        CHECK(second.last_checksum == first.last_checksum);

        acgc_metal_sink_shutdown();
        acgc_metal_sink_shutdown();
        acgc_metal_sink_get_snapshot(&after_shutdown);
        CHECK(after_shutdown.initialized == 0);
        CHECK(after_shutdown.available == 0);
        CHECK(acgc_metal_sink_submit(&output) ==
              ACGC_METAL_SINK_NOT_INITIALIZED);
    }

    puts("Metal sink: PASS (synchronous offscreen completion/readback; no live-frame claim)");
    return 0;
}
