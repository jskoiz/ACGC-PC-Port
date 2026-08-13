#include "pc_gx_internal.h"

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

_Static_assert(
    sizeof(AcgcGxSemanticPacketV3) == ACGC_GX_SEMANTIC_PACKET_V3_SIZE,
    "v3 packet ABI must remain fixed-width"
);
_Static_assert(
    sizeof(AcgcGxSemanticPacketV4) == ACGC_GX_SEMANTIC_PACKET_V4_SIZE,
    "v4 packet must remain fixed-width"
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

int main(void) {
    AcgcGxSemanticPacketV3 v3_packet;
    AcgcGxSemanticPacketV4 v4_packet;

    set_v3_common_state();
    CHECK(pc_gx_build_semantic_packet_v3_fixture(0, 3, &v3_packet) == 1);
    CHECK(v3_packet.version == ACGC_GX_SEMANTIC_PACKET_V3_VERSION);
    CHECK(v3_packet.byte_size == ACGC_GX_SEMANTIC_PACKET_V3_SIZE);
    CHECK(acgc_gx_semantic_packet_v3_validate(&v3_packet) == 1);

    CHECK(pc_gx_build_semantic_packet_v4_fixture(0, 3, &v4_packet) == 1);
    CHECK(v4_packet.version == ACGC_GX_SEMANTIC_PACKET_V4_VERSION);
    CHECK(v4_packet.byte_size == ACGC_GX_SEMANTIC_PACKET_V4_SIZE);
    CHECK(v4_packet.state_mask ==
          ACGC_GX_SEMANTIC_PACKET_V4_STATE_SUPPORTED);
    CHECK(v4_packet.alpha_update_enable ==
          ACGC_GX_SEMANTIC_V4_ALPHA_UPDATE_ENABLED);
    CHECK(acgc_gx_semantic_packet_v4_validate(&v4_packet) == 1);

    GXSetAlphaUpdate(GX_FALSE);
    CHECK(g_gx.alpha_update_enable == 0);
    CHECK(pc_gx_build_semantic_packet_v3_fixture(0, 3, &v3_packet) == 0);
    CHECK(pc_gx_build_semantic_packet_v4_fixture(0, 3, &v4_packet) == 1);
    CHECK(v4_packet.alpha_update_enable ==
          ACGC_GX_SEMANTIC_V4_ALPHA_UPDATE_DISABLED);
    CHECK(acgc_gx_semantic_packet_v4_validate(&v4_packet) == 1);

    v4_packet.alpha_update_enable = UINT32_C(2);
    CHECK(acgc_gx_semantic_packet_v4_validate(&v4_packet) == 0);
    v4_packet.alpha_update_enable =
        ACGC_GX_SEMANTIC_V4_ALPHA_UPDATE_DISABLED;
    v4_packet.state_mask = ACGC_GX_SEMANTIC_PACKET_V3_STATE_SUPPORTED;
    CHECK(acgc_gx_semantic_packet_v4_validate(&v4_packet) == 0);

    set_v3_common_state();
    g_gx.alpha_update_enable = 2;
    memset(&v4_packet, 0xA5, sizeof(v4_packet));
    CHECK(pc_gx_build_semantic_packet_v4_fixture(0, 3, &v4_packet) == 0);
    CHECK(v4_packet.version == 0);
    CHECK(v4_packet.byte_size == 0);

    puts("pc GX semantic v4 alpha-state tests: PASS (V3 preservation and V4 alpha write-mask contract)");
    puts("proof boundary: bounded CPU packet construction and validation only; no consumer, Metal, or playability claim");
    return 0;
}
