#include "pc_gx_internal.h"
#include "acgc/metal_packet_consumer.h"

#include <dolphin/gx/GXEnum.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void pc_gx_tev_seq_reset(void) {
}

PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    return NULL;
}

extern void GXSetAlphaUpdate(GXBool enable);
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
extern int pc_gx_build_semantic_packet_v4_fixture(
    int first_vertex,
    int vertex_count,
    AcgcGxSemanticPacketV4* packet
);
extern void pc_gx_set_semantic_packet_v4_handoff(
    AcgcMetalPacketConsumerV4HandoffCallback callback,
    void* context
);
extern void pc_gx_clear_semantic_packet_v4_handoff(void);
extern int pc_gx_try_handoff_semantic_packet_v4(
    int first_vertex,
    int vertex_count
);

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

static void set_v3_common_state(void) {
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
    g_gx.vertex_buffer[1].position[0] = -0.75f;
    g_gx.vertex_buffer[1].position[1] = -0.65f;
    g_gx.vertex_buffer[2].position[0] = 0.75f;
    g_gx.vertex_buffer[2].position[1] = -0.65f;
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

static void dispatch_v4_to_consumer(
    void* context,
    const AcgcGxSemanticPacketV4* packet
) {
    acgc_metal_packet_consumer_handoff_v4(context, packet);
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

    set_v3_common_state();
    CHECK(pc_gx_build_semantic_packet_v3_fixture(0, 3, &v3_packet) == 1);
    CHECK(acgc_metal_packet_consumer_prepare_v3(
        &v3_packet, NULL, &output
    ) == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.semantic_version == ACGC_GX_SEMANTIC_PACKET_V3_VERSION);
    CHECK(output.v3_extension_rendering_status ==
          ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED);
    CHECK(output.v4_extension_rendering_status == 0);

    CHECK(pc_gx_build_semantic_packet_v4_fixture(0, 3, &v4_packet) == 1);
    CHECK(v4_packet.alpha_update_enable ==
          ACGC_GX_SEMANTIC_V4_ALPHA_UPDATE_ENABLED);
    CHECK(acgc_gx_semantic_packet_v4_validate(&v4_packet) == 1);
    acgc_metal_packet_consumer_handoff_v4(&handoff, &v4_packet);
    CHECK(probe.calls == 1);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(probe.has_output == 1);
    CHECK(probe.output.semantic_version == ACGC_GX_SEMANTIC_PACKET_V4_VERSION);
    CHECK(probe.output.v3_extension_rendering_status ==
          ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED);
    CHECK(probe.output.v4_extension_rendering_status == 0);
    CHECK(probe.output.alpha_write_enabled == 1);
    CHECK(probe.output.geometry.vertex_count == 3);
    CHECK(acgc_renderer_geometry_validate(&probe.output.geometry) == 1);

    GXSetAlphaUpdate(GX_FALSE);
    CHECK(g_gx.alpha_update_enable == 0);
    CHECK(pc_gx_build_semantic_packet_v3_fixture(0, 3, &v3_packet) == 0);
    CHECK(pc_gx_build_semantic_packet_v4_fixture(0, 3, &v4_packet) == 1);
    CHECK(v4_packet.alpha_update_enable ==
          ACGC_GX_SEMANTIC_V4_ALPHA_UPDATE_DISABLED);
    CHECK(acgc_gx_semantic_packet_v4_validate(&v4_packet) == 1);
    acgc_metal_packet_consumer_handoff_v4(&handoff, &v4_packet);
    CHECK(probe.calls == 2);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(probe.has_output == 1);
    CHECK(probe.output.semantic_version == ACGC_GX_SEMANTIC_PACKET_V4_VERSION);
    CHECK(probe.output.v4_extension_rendering_status == 0);
    CHECK(probe.output.alpha_write_enabled == 0);

    /* V4 may carry a valid non-indexed GX texture map while the Apple
     * consumer explicitly leaves the texture/TEV extension unrendered. */
    g_gx.gl_textures[2] = (GLuint)19;
    g_gx.tex_obj_w[2] = 64;
    g_gx.tex_obj_h[2] = 64;
    g_gx.tex_obj_fmt[2] = GX_TF_RGBA8;
    g_gx.tev_stages[0].tex_map = 2;
    CHECK(pc_gx_build_semantic_packet_v3_fixture(0, 3, &v3_packet) == 0);
    CHECK(pc_gx_build_semantic_packet_v4_fixture(0, 3, &v4_packet) == 1);
    CHECK(v4_packet.alpha_update_enable ==
          ACGC_GX_SEMANTIC_V4_ALPHA_UPDATE_DISABLED);

    /* The V4 Apple fixture uses bounded defaults for raster/depth state that
     * is not represented in the packet; V3 remains fail-closed. */
    g_gx.alpha_comp0 = GX_LESS;
    g_gx.alpha_comp1 = GX_GREATER;
    g_gx.alpha_op = GX_AOP_OR;
    g_gx.alpha_ref0 = 8;
    g_gx.alpha_ref1 = 144;
    g_gx.z_compare_enable = 0;
    g_gx.z_update_enable = 0;
    g_gx.cull_mode = GX_CULL_BACK;
    CHECK(pc_gx_build_semantic_packet_v3_fixture(0, 3, &v3_packet) == 0);
    CHECK(pc_gx_build_semantic_packet_v4_fixture(0, 3, &v4_packet) == 1);

    /* Color writes are still fixed-on in the Apple fixture, so unlike the
     * unencoded alpha/depth/cull state this remains a V4 rejection. */
    g_gx.color_update_enable = 0;
    CHECK(pc_gx_build_semantic_packet_v4_fixture(0, 3, &v4_packet) == 0);
    g_gx.color_update_enable = 1;
    CHECK(pc_gx_build_semantic_packet_v4_fixture(0, 3, &v4_packet) == 1);

    /* Exercise the same typed V4 builder/dispatch seam used by GX flush. */
    pc_gx_set_semantic_packet_v4_handoff(dispatch_v4_to_consumer, &handoff);
    CHECK(pc_gx_try_handoff_semantic_packet_v4(0, 3) == 1);
    CHECK(probe.calls == 3);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(probe.has_output == 1);
    CHECK(probe.output.semantic_version == ACGC_GX_SEMANTIC_PACKET_V4_VERSION);
    CHECK(probe.output.v3_extension_rendering_status ==
          ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED);
    CHECK(probe.output.v4_extension_rendering_status == 0);
    CHECK(probe.output.alpha_write_enabled == 0);
    pc_gx_clear_semantic_packet_v4_handoff();

    invalid_packet = v4_packet;
    invalid_packet.alpha_update_enable = UINT32_C(2);
    acgc_metal_packet_consumer_handoff_v4(&handoff, &invalid_packet);
    CHECK(probe.calls == 4);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET);
    CHECK(probe.has_output == 0);

    invalid_packet = v4_packet;
    invalid_packet.state_mask = ACGC_GX_SEMANTIC_PACKET_V3_STATE_SUPPORTED;
    acgc_metal_packet_consumer_handoff_v4(&handoff, &invalid_packet);
    CHECK(probe.calls == 5);
    CHECK(probe.status == ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET);
    CHECK(probe.has_output == 0);

    acgc_metal_packet_consumer_unregister_runtime_callback(&handoff);
    puts("pc GX V4 consumer fixture: PASS (typed V4 acceptance, blend mapping, alpha write mask, and fail-closed validation)");
    puts("proof boundary: bounded CPU consumer preparation only; live Metal/device/pixel/playability proof remains separate");
    return 0;
}
