#include "pc_gx_internal.h"
#include "acgc/metal_packet_consumer.h"
#include "acgc/pc_metal_runtime.h"

#include <dolphin/gx/GXEnum.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

void pc_gx_tev_seq_reset(void) {
}

PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    return NULL;
}

extern void GXSetTexCoordGen2(
    u32 dst,
    u32 func,
    u32 src,
    u32 mtx,
    GXBool normalize,
    u32 postmtx
);
extern int pc_gx_build_semantic_packet_v2_fixture(
    int first_vertex,
    int vertex_count,
    AcgcGxSemanticPacketV2* packet
);

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

static void set_v2_state(void) {
    int index;
    int component;

    set_identity_state();
    g_gx.projection_type = GX_PERSPECTIVE;
    g_gx.cull_mode = GX_CULL_NONE;
    g_gx.num_chans = 1;
    g_gx.num_tex_gens = 2;
    g_gx.num_tev_stages = 2;
    g_gx.z_compare_enable = 1;
    g_gx.z_compare_func = GX_LEQUAL;
    g_gx.z_update_enable = 1;
    g_gx.color_update_enable = 1;
    g_gx.alpha_update_enable = 1;
    g_gx.blend_mode = GX_BM_NONE;
    g_gx.blend_src = GX_BL_ONE;
    g_gx.blend_dst = GX_BL_ZERO;
    g_gx.blend_logic_op = GX_LO_CLEAR;

    for (index = 0; index < 2; index++) {
        int color = index * 2;
        int alpha = color + 1;

        g_gx.chan_ctrl_enable[color] = GX_FALSE;
        g_gx.chan_ctrl_enable[alpha] = GX_FALSE;
        g_gx.chan_ctrl_amb_src[color] = GX_SRC_REG;
        g_gx.chan_ctrl_amb_src[alpha] = GX_SRC_REG;
        g_gx.chan_ctrl_mat_src[color] = GX_SRC_REG;
        g_gx.chan_ctrl_mat_src[alpha] = GX_SRC_REG;
        g_gx.chan_ctrl_light_mask[color] = 0;
        g_gx.chan_ctrl_light_mask[alpha] = 0;
        g_gx.chan_ctrl_diff_fn[color] = GX_DF_NONE;
        g_gx.chan_ctrl_diff_fn[alpha] = GX_DF_NONE;
        g_gx.chan_ctrl_attn_fn[color] = GX_AF_NONE;
        g_gx.chan_ctrl_attn_fn[alpha] = GX_AF_NONE;
        for (component = 0; component < 4; component++) {
            g_gx.chan_amb_color[index][component] = 0.0f;
            g_gx.chan_mat_color[index][component] = 1.0f;
        }
    }

    /* These calls also make the normalize/post-matrix state explicitly known. */
    GXSetTexCoordGen2(
        0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY
    );
    GXSetTexCoordGen2(
        1, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY
    );
    g_gx.gl_textures[0] = (GLuint)17;
    g_gx.gl_textures[1] = (GLuint)18;
    g_gx.tex_obj_w[0] = 4;
    g_gx.tex_obj_h[0] = 4;
    g_gx.tex_obj_fmt[0] = GX_TF_RGBA8;
    g_gx.tex_obj_w[1] = 8;
    g_gx.tex_obj_h[1] = 8;
    g_gx.tex_obj_fmt[1] = GX_TF_RGBA8;

    for (index = 0; index < 4; index++) {
        g_gx.tev_swap_table[index].r = 0;
        g_gx.tev_swap_table[index].g = 1;
        g_gx.tev_swap_table[index].b = 2;
        g_gx.tev_swap_table[index].a = 3;
    }
    for (index = 0; index < 2; index++) {
        PCGXTevStage* stage = &g_gx.tev_stages[index];

        stage->color_a = GX_CC_ZERO;
        stage->color_b = GX_CC_ZERO;
        stage->color_c = GX_CC_ZERO;
        stage->color_d = index == 0 ? GX_CC_TEXC : GX_CC_CPREV;
        stage->alpha_a = GX_CA_ZERO;
        stage->alpha_b = GX_CA_ZERO;
        stage->alpha_c = GX_CA_ZERO;
        stage->alpha_d = index == 0 ? GX_CA_TEXA : GX_CA_APREV;
        stage->color_op = GX_TEV_ADD;
        stage->color_bias = GX_TB_ZERO;
        stage->color_scale = GX_CS_SCALE_1;
        stage->color_clamp = GX_TRUE;
        stage->color_out = GX_TEVPREV;
        stage->alpha_op = GX_TEV_ADD;
        stage->alpha_bias = GX_TB_ZERO;
        stage->alpha_scale = GX_CS_SCALE_1;
        stage->alpha_clamp = GX_TRUE;
        stage->alpha_out = GX_TEVPREV;
        stage->tex_coord = index == 0 ? GX_TEXCOORD0 : GX_TEXCOORD1;
        stage->tex_map = index == 0 ? GX_TEXMAP0 : GX_TEXMAP1;
        stage->color_chan = GX_COLOR0A0;
        stage->k_color_sel = GX_TEV_KCSEL_1_4;
        stage->k_alpha_sel = GX_TEV_KASEL_1;
        stage->ras_swap = GX_TEV_SWAP0;
        stage->tex_swap = GX_TEV_SWAP0;
    }
}

typedef struct PacketVersionProbe {
    unsigned int calls;
    uint32_t version;
    uint32_t byte_size;
} PacketVersionProbe;

static void record_packet_version(
    void* context,
    const AcgcGxSemanticPacket* packet
) {
    PacketVersionProbe* probe = (PacketVersionProbe*)context;

    if (probe == NULL) {
        return;
    }
    probe->calls++;
    if (packet != NULL) {
        probe->version = packet->version;
        probe->byte_size = packet->byte_size;
    }
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
    PacketVersionProbe version_probe = { 0 };
    AcgcGxSemanticPacketV2 packet_v2;

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

    /* The existing callback remains an explicit v1 boundary. */
    pc_gx_set_semantic_packet_handoff(record_packet_version, &version_probe);
    set_identity_state();
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 1);
    CHECK(version_probe.calls == 1);
    CHECK(version_probe.version == ACGC_GX_SEMANTIC_PACKET_VERSION);
    CHECK(version_probe.byte_size == ACGC_GX_SEMANTIC_PACKET_SIZE);
    set_v2_state();
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    CHECK(version_probe.calls == 1);
    pc_gx_set_semantic_packet_handoff(
        acgc_metal_packet_consumer_handoff,
        &handoff
    );

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

    /* Resident GL texture objects are allowed while no draw state uses them. */
    set_identity_state();
    g_gx.gl_textures[0] = 1;
    reset_handoff(&handoff, &output);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 1);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OK);

    /* An actively textured draw remains outside packet v1. */
    set_identity_state();
    g_gx.gl_textures[0] = 1;
    g_gx.tev_stages[0].tex_coord = GX_TEXCOORD0;
    g_gx.tev_stages[0].tex_map = GX_TEXMAP0;
    reset_handoff(&handoff, &output);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID);

    /* Non-pass-through TEV state remains on the legacy GL path. */
    set_identity_state();
    g_gx.tev_stages[0].color_d = GX_CC_CPREV;
    reset_handoff(&handoff, &output);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID);

    /* Non-finite decoded vertex data is rejected by the fixed packet validator. */
    set_identity_state();
    g_gx.vertex_buffer[1].position[0] = NAN;
    reset_handoff(&handoff, &output);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID);

    /* Fog is not represented in packet v1 and therefore fails closed. */
    set_identity_state();
    g_gx.fog_type = GX_FOG_PERSP_LIN;
    reset_handoff(&handoff, &output);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID);

    /* The packet has no lighting payload, so configured channels fail closed. */
    set_identity_state();
    g_gx.num_chans = 1;
    reset_handoff(&handoff, &output);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID);

    /* The bounded v2 constructor is fixture-only until a versioned consumer exists. */
    set_v2_state();
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 1);
    CHECK(acgc_gx_semantic_packet_v2_validate(&packet_v2) == 1);
    CHECK(packet_v2.base.version == ACGC_GX_SEMANTIC_PACKET_V2_VERSION);
    CHECK(packet_v2.base.byte_size == ACGC_GX_SEMANTIC_PACKET_V2_SIZE);
    CHECK(packet_v2.state_mask == ACGC_GX_SEMANTIC_V2_STATE_SUPPORTED);
    CHECK(packet_v2.channel_count == 1);
    CHECK(packet_v2.texture_generator_count == 2);
    CHECK(packet_v2.tev_stage_count == 2);
    CHECK(packet_v2.texture_generators[0].texture_key == 17);
    CHECK(packet_v2.texture_generators[1].texture_key == 18);
    CHECK(packet_v2.tev_stages[0].color_input[3] ==
          ACGC_GX_SEMANTIC_V2_COLOR_INPUT_TEXTURE);
    CHECK(packet_v2.tev_stages[1].color_input[3] ==
          ACGC_GX_SEMANTIC_V2_COLOR_INPUT_PREVIOUS);
    packet_v2.state_mask &= ~ACGC_GX_SEMANTIC_V2_STATE_FOG_KNOWN;
    CHECK(acgc_gx_semantic_packet_v2_validate(&packet_v2) == 0);
    set_v2_state();
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 1);
    packet_v2.base.material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_TEXTURE0;
    packet_v2.base.material.texture0_key = 17;
    CHECK(acgc_gx_semantic_packet_v2_validate(&packet_v2) == 0);

    /* The old consumer is not invoked with a v2 packet. */
    reset_handoff(&handoff, &output);
    handoff.status = ACGC_METAL_PACKET_CONSUMER_OK;
    pc_gx_set_semantic_packet_handoff(
        acgc_metal_packet_consumer_handoff,
        &handoff
    );
    set_v2_state();
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OK);

    /* Unknown lighting, fog, indirect, alpha, dynamic, and TEV state fail closed. */
    set_v2_state();
    g_gx.chan_ctrl_enable[0] = GX_TRUE;
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 0);
    set_v2_state();
    g_gx.num_chans = 3;
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 0);
    set_v2_state();
    g_gx.fog_type = GX_FOG_PERSP_LIN;
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 0);
    set_v2_state();
    g_gx.num_ind_stages = 1;
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 0);
    set_v2_state();
    g_gx.alpha_comp0 = GX_LESS;
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 0);
    set_v2_state();
    g_gx.cull_mode = GX_CULL_BACK;
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 0);
    set_v2_state();
    g_gx.blend_src = GX_BL_SRCALPHA;
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 0);
    set_v2_state();
    g_gx.tex_gen_type[0] = GX_TG_BUMP0;
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 0);
    set_v2_state();
    g_gx.tev_stages[0].k_color_sel = GX_TEV_KCSEL_K0;
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 0);
    set_v2_state();
    g_gx.tev_stages[0].color_op = GX_TEV_COMP_R8_GT;
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 0);
    set_v2_state();
    g_gx.gl_textures[0] = 0;
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 0);
    set_v2_state();
    g_gx.current_mtx = 10;
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 0);
    set_v2_state();
    g_gx.projection_mtx[0][0] = NAN;
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet_v2) == 0);

    /* The production bridge uses only fixed storage and the existing handoff. */
    {
        AcgcPcMetalRuntimeSnapshot snapshot;

        pc_metal_runtime_init();
        pc_metal_runtime_get_snapshot(&snapshot);
        CHECK(snapshot.registered == 1);
        CHECK(snapshot.handoff_count == 0);

        set_identity_state();
        CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 1);
        pc_metal_runtime_get_snapshot(&snapshot);
        CHECK(snapshot.handoff_count == 1);
        CHECK(snapshot.accepted_count == 1);
        CHECK(snapshot.rejected_count == 0);
        CHECK(snapshot.last_status == ACGC_METAL_PACKET_CONSUMER_OK);

        set_identity_state();
        g_gx.current_primitive = GX_QUADS;
        g_gx.expected_vertex_count = 4;
        g_gx.current_vertex_idx = 4;
        CHECK(pc_gx_try_handoff_semantic_vertices(0, 4) == 1);
        pc_metal_runtime_get_snapshot(&snapshot);
        CHECK(snapshot.handoff_count == 2);
        CHECK(snapshot.accepted_count == 1);
        CHECK(snapshot.rejected_count == 1);
        CHECK(snapshot.last_status == ACGC_METAL_PACKET_CONSUMER_UNSUPPORTED_TOPOLOGY);

        pc_metal_runtime_shutdown();
        pc_metal_runtime_get_snapshot(&snapshot);
        CHECK(snapshot.registered == 0);
        set_identity_state();
        CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    }

    pc_gx_clear_semantic_packet_handoff();
    set_identity_state();
    reset_handoff(&handoff, &output);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 0);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID);
    puts("pc GX semantic handoff tests: PASS (complete packet route, incomplete capture, and unsupported state fail closed; GL fallback untouched)");
    puts("proof boundary: CPU packet preparation only; no live Metal device or game frame is claimed");
    return 0;
}
