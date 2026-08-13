#include "pc_gx_internal.h"
#include "acgc/metal_packet_consumer.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
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
extern void GXLoadTexMtxImm(const void* mtx, u32 id, u32 type);
extern int pc_gx_build_semantic_packet_v3_fixture(
    int first_vertex,
    int vertex_count,
    AcgcGxSemanticPacketV3* packet
);
extern void pc_gx_set_semantic_packet_v3_handoff(
    AcgcMetalPacketConsumerV3HandoffCallback callback,
    void* context
);
extern void pc_gx_clear_semantic_packet_v3_handoff(void);
extern int pc_gx_try_handoff_semantic_packet_v2(
    int first_vertex,
    int vertex_count
);
extern int pc_gx_try_handoff_semantic_packet_v3(
    int first_vertex,
    int vertex_count
);

_Static_assert(
    sizeof(AcgcGxSemanticPacketV3) == ACGC_GX_SEMANTIC_PACKET_V3_SIZE,
    "v3 packet must remain fixed-width"
);

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static void set_v3_state(void) {
    PCGXTevStage* stage;
    float texture_matrix[12] = {
        1.0f, 0.25f, 0.0f, 0.1f,
        0.0f, 1.0f, 0.0f, 0.2f,
        0.0f, 0.0f, 1.0f, 0.0f
    };
    int index;
    int component;

    memset(&g_gx, 0, sizeof(g_gx));
    g_gx.current_primitive = GX_TRIANGLES;
    g_gx.current_mtx = 0;
    g_gx.expected_vertex_count = 3;
    g_gx.current_vertex_idx = 3;
    g_gx.num_chans = 1;
    g_gx.num_tex_gens = 1;
    g_gx.num_tev_stages = 1;
    g_gx.projection_type = GX_PERSPECTIVE;
    g_gx.fog_type = GX_FOG_NONE;
    g_gx.alpha_comp0 = GX_ALWAYS;
    g_gx.alpha_comp1 = GX_ALWAYS;
    g_gx.alpha_op = GX_AOP_AND;
    g_gx.z_compare_enable = 1;
    g_gx.z_compare_func = GX_LEQUAL;
    g_gx.z_update_enable = 1;
    g_gx.color_update_enable = 1;
    g_gx.alpha_update_enable = 1;
    g_gx.cull_mode = GX_CULL_NONE;
    g_gx.blend_mode = GX_BM_BLEND;
    g_gx.blend_src = GX_BL_SRCALPHA;
    g_gx.blend_dst = GX_BL_INVSRCALPHA;
    g_gx.blend_logic_op = GX_LO_NOOP;

    for (index = 0; index < 4; index++) {
        g_gx.projection_mtx[index][index] = 1.0f;
    }
    for (index = 0; index < 3; index++) {
        g_gx.pos_mtx[0][index][index] = 1.0f;
        g_gx.nrm_mtx[0][index][index] = 1.0f;
    }

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

    GXSetTexCoordGen2(
        0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY
    );
    GXLoadTexMtxImm(texture_matrix, GX_TEXMTX0, 0);
    g_gx.gl_textures[0] = (GLuint)17;
    g_gx.tex_obj_w[0] = 64;
    g_gx.tex_obj_h[0] = 64;
    g_gx.tex_obj_fmt[0] = GX_TF_RGBA8;

    for (index = 0; index < 4; index++) {
        g_gx.tev_swap_table[index].r = 0;
        g_gx.tev_swap_table[index].g = 1;
        g_gx.tev_swap_table[index].b = 2;
        g_gx.tev_swap_table[index].a = 3;
    }
    stage = &g_gx.tev_stages[0];
    stage->color_a = GX_CC_ZERO;
    stage->color_b = GX_CC_ZERO;
    stage->color_c = GX_CC_ZERO;
    stage->color_d = GX_CC_TEXC;
    stage->alpha_a = GX_CA_ZERO;
    stage->alpha_b = GX_CA_ZERO;
    stage->alpha_c = GX_CA_ZERO;
    stage->alpha_d = GX_CA_TEXA;
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
    stage->tex_coord = 0;
    stage->tex_map = 0;
    stage->color_chan = GX_COLOR0A0;
    stage->k_color_sel = GX_TEV_KCSEL_1;
    stage->k_alpha_sel = GX_TEV_KASEL_1;
    stage->ras_swap = GX_TEV_SWAP0;
    stage->tex_swap = GX_TEV_SWAP0;

    g_gx.vertex_buffer[0].position[1] = 0.75f;
    g_gx.vertex_buffer[0].color0[0] = 0xF9;
    g_gx.vertex_buffer[0].color0[1] = 0x41;
    g_gx.vertex_buffer[0].color0[2] = 0x44;
    g_gx.vertex_buffer[0].color0[3] = 0xFF;
    g_gx.vertex_buffer[1].position[0] = -0.75f;
    g_gx.vertex_buffer[1].position[1] = -0.65f;
    g_gx.vertex_buffer[1].color0[0] = 0x43;
    g_gx.vertex_buffer[1].color0[1] = 0xAA;
    g_gx.vertex_buffer[1].color0[2] = 0x8B;
    g_gx.vertex_buffer[1].color0[3] = 0xFF;
    g_gx.vertex_buffer[2].position[0] = 0.75f;
    g_gx.vertex_buffer[2].position[1] = -0.65f;
    g_gx.vertex_buffer[2].color0[0] = 0x57;
    g_gx.vertex_buffer[2].color0[1] = 0x75;
    g_gx.vertex_buffer[2].color0[2] = 0x90;
    g_gx.vertex_buffer[2].color0[3] = 0xFF;
}

typedef struct V3Probe {
    unsigned int calls;
    AcgcGxSemanticPacketV3 packet;
} V3Probe;

static void record_v3_packet(void* context, const AcgcGxSemanticPacketV3* packet) {
    V3Probe* probe = (V3Probe*)context;

    if (probe == NULL) {
        return;
    }
    probe->calls++;
    if (packet != NULL) {
        memcpy(&probe->packet, packet, sizeof(probe->packet));
    }
}

typedef struct ConsumerProbe {
    unsigned int calls;
    AcgcMetalPacketConsumerOutput output;
    AcgcMetalPacketConsumerStatus status;
    int has_output;
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
    if (output != NULL) {
        memcpy(&probe->output, output, sizeof(probe->output));
    }
}

int main(void) {
    AcgcGxSemanticPacketV3 packet;
    AcgcMetalPacketConsumerHandoffContext handoff;
    AcgcMetalPacketConsumerOutput output;
    V3Probe v3_probe = { 0 };
    ConsumerProbe consumer_probe = { 0 };

    set_v3_state();
    pc_gx_clear_semantic_packet_v3_handoff();
    CHECK(pc_gx_try_handoff_semantic_packet_v2(0, 3) == 0);
    CHECK(pc_gx_build_semantic_packet_v3_fixture(0, 3, &packet) == 1);
    CHECK(packet.version == ACGC_GX_SEMANTIC_PACKET_V3_VERSION);
    CHECK(packet.byte_size == ACGC_GX_SEMANTIC_PACKET_V3_SIZE);
    CHECK(packet.state_mask == ACGC_GX_SEMANTIC_PACKET_V3_STATE_SUPPORTED);
    CHECK(packet.blend.mode == ACGC_GX_SEMANTIC_V3_BLEND_MODE_BLEND);
    CHECK(packet.blend.source_factor ==
          ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_ALPHA);
    CHECK(packet.blend.destination_factor ==
          ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_ALPHA);
    CHECK(packet.blend.logic_op == ACGC_GX_SEMANTIC_V3_LOGIC_NOOP);
    CHECK(packet.texture_matrix_count == 1);
    CHECK(packet.texture_matrices[0].generator_index == 0);
    CHECK(packet.texture_matrices[0].matrix_slot == 0);
    CHECK(packet.texture_matrices[0].matrix[1] == UINT32_C(0x3E800000));
    CHECK(packet.texture_matrices[0].matrix[3] == UINT32_C(0x3DCCCCCD));
    CHECK(acgc_gx_semantic_packet_v3_validate(&packet) == 1);

    pc_gx_set_semantic_packet_v3_handoff(record_v3_packet, &v3_probe);
    CHECK(pc_gx_try_handoff_semantic_packet_v3(0, 3) == 1);
    CHECK(v3_probe.calls == 1);
    CHECK(v3_probe.packet.blend.logic_op == ACGC_GX_SEMANTIC_V3_LOGIC_NOOP);

    memset(&handoff, 0, sizeof(handoff));
    memset(&output, 0, sizeof(output));
    handoff.output = &output;
    handoff.status = ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    CHECK(acgc_metal_packet_consumer_register_runtime_callback(
        &handoff,
        record_consumer_output,
        &consumer_probe
    ) == 1);
    acgc_metal_packet_consumer_handoff_v3(&handoff, &packet);
    CHECK(consumer_probe.calls == 1);
    CHECK(consumer_probe.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(consumer_probe.has_output == 1);
    CHECK(consumer_probe.output.semantic_version ==
          ACGC_GX_SEMANTIC_PACKET_V3_VERSION);
    CHECK(consumer_probe.output.v3_extension_rendering_status ==
          ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED);
    CHECK(consumer_probe.output.geometry.vertex_count == 3);

    packet.blend.logic_op = UINT32_C(99);
    acgc_metal_packet_consumer_handoff_v3(&handoff, &packet);
    CHECK(consumer_probe.calls == 2);
    CHECK(consumer_probe.status == ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET);
    CHECK(consumer_probe.has_output == 0);

    acgc_metal_packet_consumer_unregister_runtime_callback(&handoff);
    pc_gx_clear_semantic_packet_v3_handoff();
    puts("pc GX semantic v3 handoff tests: PASS (live blend/matrix state forwarding and fail-closed consumer)");
    puts("proof boundary: bounded CPU state packet and callback only; no Metal encode, pixel, or playability claim");
    return 0;
}
