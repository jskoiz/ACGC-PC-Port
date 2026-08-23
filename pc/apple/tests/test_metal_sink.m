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

static int test_cpu_contract(
    AcgcMetalPacketConsumerOutput* output,
    AcgcMetalSinkStatus* init_status
) {
    AcgcMetalPacketConsumerOutput invalid_output;
    AcgcMetalPacketConsumerOutput multi_output;
    AcgcMetalSinkSnapshot snapshot;
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
        CHECK(first.submit_count == 3);
        CHECK(first.completed_count == 2);
        CHECK(first.readback_count == 2);
        CHECK(first.last_status == ACGC_METAL_SINK_OK);
        CHECK(first.last_pixel_rgba8 != UINT32_C(0x000000FF));
        CHECK((first.last_pixel_rgba8 & UINT32_C(0xFF)) == UINT32_C(0xFF));
        CHECK(first.last_checksum != 0);

        /* A second synchronous pass must produce the same bounded readback. */
        CHECK(acgc_metal_sink_submit(&output) == ACGC_METAL_SINK_OK);
        acgc_metal_sink_get_snapshot(&second);
        CHECK(second.submit_count == 4);
        CHECK(second.completed_count == 3);
        CHECK(second.readback_count == 3);
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
