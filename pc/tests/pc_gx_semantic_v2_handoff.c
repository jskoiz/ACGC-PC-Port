#include "pc_gx_internal.h"
#include "acgc/metal_packet_consumer.h"

#include <dolphin/gx/GXEnum.h>

#include <math.h>
#include <stddef.h>
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
extern int pc_gx_build_semantic_packet_v2_fixture(
    int first_vertex,
    int vertex_count,
    AcgcGxSemanticPacketV2* packet
);
extern void pc_gx_set_semantic_packet_v2_handoff(
    AcgcMetalPacketConsumerV2HandoffCallback callback,
    void* context
);
extern void pc_gx_clear_semantic_packet_v2_handoff(void);
extern int pc_gx_try_handoff_semantic_packet_v2(
    int first_vertex,
    int vertex_count
);

_Static_assert(
    offsetof(AcgcGxSemanticPacketV2, state_mask) ==
        sizeof(AcgcGxSemanticPacket),
    "v2 extension must follow the v1 prefix"
);
_Static_assert(
    sizeof(AcgcGxSemanticPacketV2) == ACGC_GX_SEMANTIC_PACKET_V2_SIZE,
    "v2 packet must remain fixed-width"
);

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static void set_v1_state(void) {
    PCGXTevStage* stage;
    int index;

    memset(&g_gx, 0, sizeof(g_gx));
    g_gx.current_primitive = GX_TRIANGLES;
    g_gx.current_mtx = 0;
    g_gx.expected_vertex_count = 3;
    g_gx.current_vertex_idx = 3;
    g_gx.num_tev_stages = 1;
    g_gx.projection_type = GX_PERSPECTIVE;
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

static void set_v2_state(void) {
    int index;
    int component;

    set_v1_state();
    g_gx.num_chans = 1;
    g_gx.num_tex_gens = 2;
    g_gx.num_tev_stages = 2;
    g_gx.cull_mode = GX_CULL_NONE;
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
        stage->tex_coord = index;
        stage->tex_map = index;
        stage->color_chan = GX_COLOR0A0;
        stage->k_color_sel = GX_TEV_KCSEL_1_4;
        stage->k_alpha_sel = GX_TEV_KASEL_1;
        stage->ras_swap = GX_TEV_SWAP0;
        stage->tex_swap = GX_TEV_SWAP0;
    }
}

typedef struct V1Probe {
    unsigned int calls;
    uint32_t version;
    uint32_t byte_size;
} V1Probe;

static void record_v1_packet(void* context, const AcgcGxSemanticPacket* packet) {
    V1Probe* probe = (V1Probe*)context;

    if (probe == NULL) {
        return;
    }
    probe->calls++;
    if (packet != NULL) {
        probe->version = packet->version;
        probe->byte_size = packet->byte_size;
    }
}

typedef struct V2Probe {
    unsigned int calls;
    const AcgcMetalPacketConsumerOutput* output;
    AcgcMetalPacketConsumerStatus status;
} V2Probe;

static void record_v2_output(
    void* context,
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
) {
    V2Probe* probe = (V2Probe*)context;

    if (probe == NULL) {
        return;
    }
    probe->calls++;
    probe->output = output;
    probe->status = status;
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

int main(void) {
    AcgcMetalPacketConsumerHandoffContext handoff;
    AcgcMetalPacketConsumerOutput output;
    AcgcGxSemanticPacketV2 packet;
    V1Probe v1_probe = { 0 };
    V2Probe v2_probe = { 0 };

    /* The legacy callback remains a v1-only path. */
    set_v1_state();
    pc_gx_set_semantic_packet_handoff(record_v1_packet, &v1_probe);
    CHECK(pc_gx_try_handoff_semantic_vertices(0, 3) == 1);
    CHECK(v1_probe.calls == 1);
    CHECK(v1_probe.version == ACGC_GX_SEMANTIC_PACKET_VERSION);
    CHECK(v1_probe.byte_size == ACGC_GX_SEMANTIC_PACKET_SIZE);

    reset_handoff(&handoff, &output);
    CHECK(acgc_metal_packet_consumer_register_runtime_callback(
        &handoff,
        record_v2_output,
        &v2_probe
    ) == 1);
    pc_gx_set_semantic_packet_v2_handoff(
        acgc_metal_packet_consumer_handoff_v2,
        &handoff
    );

    /* A valid v2 packet reaches only the separately typed v2 consumer. */
    set_v2_state();
    CHECK(pc_gx_try_handoff_semantic_packet_v2(0, 3) == 1);
    CHECK(v2_probe.calls == 1);
    CHECK(v2_probe.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(v2_probe.output == &output);
    CHECK(output.semantic_version == ACGC_GX_SEMANTIC_PACKET_V2_VERSION);
    CHECK(output.v2_extension_rendering_status ==
          ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_RENDERED);
    CHECK(output.geometry.vertex_count == 3);
    CHECK(output.geometry.vertices[0].color_rgba8 == UINT32_C(0xF94144FF));
    CHECK(output.texture0_key == 0);
    CHECK(output.texture0_color.r == 255);
    CHECK(output.texture0_color.g == 255);
    CHECK(output.texture0_color.b == 255);
    CHECK(output.texture0_color.a == 255);
    CHECK(v1_probe.calls == 1);

    /* Unsupported PC state rejects before the v2 callback is invoked. */
    set_v2_state();
    g_gx.fog_type = GX_FOG_PERSP_LIN;
    CHECK(pc_gx_try_handoff_semantic_packet_v2(0, 3) == 0);
    CHECK(v2_probe.calls == 1);
    CHECK(handoff.status == ACGC_METAL_PACKET_CONSUMER_OK);

    /* The consumer independently rejects a wrong v2 version and bad extension. */
    set_v2_state();
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet) == 1);
    packet.base.version = ACGC_GX_SEMANTIC_PACKET_VERSION;
    acgc_metal_packet_consumer_handoff_v2(&handoff, &packet);
    CHECK(v2_probe.calls == 2);
    CHECK(v2_probe.status == ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET);
    CHECK(v2_probe.output == NULL);

    set_v2_state();
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet) == 1);
    packet.texture_generators[0].texture_key = UINT32_C(0xDEADBEEF);
    packet.texture_generators[0].sampler_key = UINT32_C(0xDEADBEEF);
    CHECK(acgc_gx_semantic_packet_v2_validate(&packet) == 1);
    acgc_metal_packet_consumer_handoff_v2(&handoff, &packet);
    CHECK(v2_probe.calls == 3);
    CHECK(v2_probe.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(v2_probe.output == &output);
    CHECK(output.texture0_key == 0);
    CHECK(output.v2_extension_rendering_status ==
          ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_RENDERED);

    packet.tev_stages[0].reserved = 1;
    acgc_metal_packet_consumer_handoff_v2(&handoff, &packet);
    CHECK(v2_probe.calls == 4);
    CHECK(v2_probe.status == ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET);
    CHECK(v2_probe.output == NULL);

    pc_gx_clear_semantic_packet_v2_handoff();
    pc_gx_clear_semantic_packet_handoff();
    puts("pc GX semantic v2 handoff tests: PASS (typed v2 route, v1 isolation, validation, and prefix-only preparation)");
    puts("proof boundary: bounded CPU packet preparation only; no live callback, Metal frame, device, or playability claim");
    return 0;
}
