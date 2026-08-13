#include "pc_gx_internal.h"
#include "acgc/metal_packet_consumer.h"

#include <dolphin/gx/GXEnum.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static void set_identity_state(void) {
    PCGXTevStage* stage;
    int index;

    memset(&g_gx, 0, sizeof(g_gx));
    g_gx.current_primitive = GX_TRIANGLES;
    g_gx.current_mtx = 0;
    g_gx.expected_vertex_count = 3;
    g_gx.current_vertex_idx = 3;
    g_gx.num_tev_stages = 1;
    g_gx.fog_type = GX_FOG_NONE;
    g_gx.alpha_comp0 = GX_ALWAYS;
    g_gx.alpha_comp1 = GX_ALWAYS;
    g_gx.alpha_op = GX_AOP_AND;

    for (index = 0; index < 4; index++) {
        g_gx.projection_mtx[index][index] = 1.0f;
    }
    for (index = 0; index < 3; index++) {
        g_gx.pos_mtx[0][index][index] = 1.0f;
        g_gx.nrm_mtx[0][index][index] = 1.0f;
    }

    stage = &g_gx.tev_stages[0];
    stage->color_a = GX_CC_ZERO;
    stage->color_b = GX_CC_ZERO;
    stage->color_c = GX_CC_ZERO;
    stage->color_d = GX_CC_RASC;
    stage->alpha_a = GX_CA_ZERO;
    stage->alpha_b = GX_CA_ZERO;
    stage->alpha_c = GX_CA_ZERO;
    stage->alpha_d = GX_CA_RASA;
    stage->color_op = GX_TEV_ADD;
    stage->color_bias = GX_TB_ZERO;
    stage->color_scale = GX_CS_SCALE_1;
    stage->color_clamp = 1;
    stage->color_out = GX_TEVPREV;
    stage->alpha_op = GX_TEV_ADD;
    stage->alpha_bias = GX_TB_ZERO;
    stage->alpha_scale = GX_CS_SCALE_1;
    stage->alpha_clamp = 1;
    stage->alpha_out = GX_TEVPREV;
    stage->tex_coord = GX_TEXCOORD_NULL;
    stage->tex_map = GX_TEXMAP_NULL;
    stage->color_chan = GX_COLOR0A0;
    stage->ras_swap = GX_TEV_SWAP0;
    stage->tex_swap = GX_TEV_SWAP0;

    g_gx.vertex_buffer[0].position[0] = 0.0f;
    g_gx.vertex_buffer[0].position[1] = 0.75f;
    g_gx.vertex_buffer[0].color0[0] = 0xF9;
    g_gx.vertex_buffer[0].color0[1] = 0x41;
    g_gx.vertex_buffer[0].color0[2] = 0x44;
    g_gx.vertex_buffer[0].color0[3] = 0xFF;
    g_gx.vertex_buffer[0].normal[1] = 1.0f;
    g_gx.vertex_buffer[0].texcoord[0][0] = 0.5f;

    g_gx.vertex_buffer[1].position[0] = -0.75f;
    g_gx.vertex_buffer[1].position[1] = -0.65f;
    g_gx.vertex_buffer[1].color0[0] = 0x43;
    g_gx.vertex_buffer[1].color0[1] = 0xAA;
    g_gx.vertex_buffer[1].color0[2] = 0x8B;
    g_gx.vertex_buffer[1].color0[3] = 0xFF;
    g_gx.vertex_buffer[1].normal[1] = 1.0f;
    g_gx.vertex_buffer[1].texcoord[0][1] = 1.0f;

    g_gx.vertex_buffer[2].position[0] = 0.75f;
    g_gx.vertex_buffer[2].position[1] = -0.65f;
    g_gx.vertex_buffer[2].color0[0] = 0x57;
    g_gx.vertex_buffer[2].color0[1] = 0x75;
    g_gx.vertex_buffer[2].color0[2] = 0x90;
    g_gx.vertex_buffer[2].color0[3] = 0xFF;
    g_gx.vertex_buffer[2].normal[1] = 1.0f;
    g_gx.vertex_buffer[2].texcoord[0][0] = 1.0f;
    g_gx.vertex_buffer[2].texcoord[0][1] = 1.0f;
}

static void reset_handoff(
    AcgcMetalPacketConsumerHandoffContext* handoff,
    AcgcMetalPacketConsumerOutput* output
) {
    memset(output, 0xA5, sizeof(*output));
    handoff->texture = NULL;
    handoff->output = output;
    handoff->status = ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    handoff->runtime_callback = NULL;
    handoff->runtime_callback_context = NULL;
}

typedef struct RuntimeCallbackProbe {
    unsigned int calls;
    void* context;
    const AcgcMetalPacketConsumerOutput* output;
    AcgcMetalPacketConsumerStatus status;
} RuntimeCallbackProbe;

static void record_runtime_callback(
    void* context,
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
) {
    RuntimeCallbackProbe* probe = (RuntimeCallbackProbe*)context;

    if (probe == NULL) {
        return;
    }
    probe->calls++;
    probe->context = context;
    probe->output = output;
    probe->status = status;
}

int main(void) {
    AcgcMetalPacketConsumerHandoffContext handoff;
    AcgcMetalPacketConsumerOutput output;
    RuntimeCallbackProbe probe = { 0 };
    RuntimeCallbackProbe replacement = { 0 };

    set_identity_state();
    reset_handoff(&handoff, &output);
    CHECK(acgc_metal_packet_consumer_register_runtime_callback(
        &handoff,
        record_runtime_callback,
        &probe
    ) == 1);
    pc_gx_set_semantic_packet_handoff(
        acgc_metal_packet_consumer_handoff,
        &handoff
    );
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 1);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.geometry.vertex_count == 3);
    CHECK(output.geometry.vertices[0].color_rgba8 == UINT32_C(0xF94144FF));
    CHECK(output.geometry.vertices[1].position_x != output.geometry.vertices[2].position_x);
    CHECK(probe.calls == 1);
    CHECK(probe.context == &probe);
    CHECK(probe.output == &output);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_OK);

    /* The Apple callback sees a fail-closed status and no invalid output. */
    set_identity_state();
    g_gx.current_primitive = GX_QUADS;
    g_gx.expected_vertex_count = 4;
    g_gx.current_vertex_idx = 4;
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 4) == 1);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_UNSUPPORTED_TOPOLOGY);
    CHECK(probe.calls == 2);
    CHECK(probe.output == NULL);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_UNSUPPORTED_TOPOLOGY);

    /* Re-registration replaces only the borrowed callback pair. */
    CHECK(acgc_metal_packet_consumer_register_runtime_callback(
        &handoff,
        record_runtime_callback,
        &replacement
    ) == 1);
    set_identity_state();
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 1);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(probe.calls == 2);
    CHECK(replacement.calls == 1);
    CHECK(replacement.context == &replacement);
    CHECK(replacement.output == &output);
    CHECK(replacement.status == ACGC_METAL_PACKET_CONSUMER_OK);

    /* Unregistering does not take ownership of or clear caller-owned state. */
    acgc_metal_packet_consumer_unregister_runtime_callback(&handoff);
    CHECK(handoff.output == &output);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OK);
    set_identity_state();
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 1);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(probe.calls == 2);

    /* A short run is incomplete against GXBegin's declared vertex count. */
    set_identity_state();
    g_gx.current_vertex_idx = 2;
    reset_handoff(&handoff, &output);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 2) == 0);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID);

    /* An uncommitted GX vertex cannot cross the boundary. */
    set_identity_state();
    g_gx.in_begin = 1;
    g_gx.vertex_pending = 1;
    reset_handoff(&handoff, &output);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID);

    /* Native GL texture objects have no packet key and therefore fail closed. */
    set_identity_state();
    g_gx.gl_textures[0] = 1;
    reset_handoff(&handoff, &output);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID);

    /* Non-finite decoded vertex data is rejected by the fixed packet validator. */
    set_identity_state();
    g_gx.vertex_buffer[1].position[0] = NAN;
    reset_handoff(&handoff, &output);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID);

    /* The packet has no lighting payload, so configured channels fail closed. */
    set_identity_state();
    g_gx.num_chans = 1;
    reset_handoff(&handoff, &output);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID);

    pc_gx_clear_semantic_packet_handoff();
    set_identity_state();
    reset_handoff(&handoff, &output);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID);
    puts("pc GX semantic handoff tests: PASS (complete packet route, incomplete capture, and unsupported state fail closed; GL fallback untouched)");
    puts("proof boundary: CPU packet preparation only; no live Metal device or game frame is claimed");
    return 0;
}
