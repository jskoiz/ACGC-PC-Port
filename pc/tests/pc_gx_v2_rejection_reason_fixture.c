#include "pc_gx_internal.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
extern const char* pc_gx_semantic_v2_state_rejection_reason_fixture(void);
extern int pc_gx_semantic_v2_state_is_supported_fixture(void);
extern const char* pc_gx_semantic_v2_rejection_reason_fixture(
    int first_vertex,
    int vertex_count,
    int expected_vertex_count
);

void pc_gx_tev_seq_reset(void) {
}

PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    return NULL;
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static void set_supported_state(void) {
    PCGXTevStage* stage;
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
    g_gx.num_ind_stages = 0;
    g_gx.projection_type = GX_PERSPECTIVE;
    g_gx.fog_type = GX_FOG_NONE;
    g_gx.alpha_comp0 = GX_ALWAYS;
    g_gx.alpha_comp1 = GX_ALWAYS;
    g_gx.alpha_op = GX_AOP_AND;
    g_gx.alpha_ref0 = 0;
    g_gx.alpha_ref1 = 0;
    g_gx.blend_mode = GX_BM_NONE;
    g_gx.blend_src = GX_BL_ONE;
    g_gx.blend_dst = GX_BL_ZERO;
    g_gx.blend_logic_op = GX_LO_CLEAR;
    g_gx.z_compare_enable = GX_TRUE;
    g_gx.z_compare_func = GX_LEQUAL;
    g_gx.z_update_enable = GX_TRUE;
    g_gx.color_update_enable = GX_TRUE;
    g_gx.alpha_update_enable = GX_TRUE;
    g_gx.cull_mode = GX_CULL_NONE;

    for (index = 0; index < 4; index++) {
        g_gx.projection_mtx[index][index] = 1.0f;
    }
    for (index = 0; index < 3; index++) {
        g_gx.pos_mtx[0][index][index] = 1.0f;
        g_gx.nrm_mtx[0][index][index] = 1.0f;
    }

    g_gx.chan_ctrl_enable[0] = GX_FALSE;
    g_gx.chan_ctrl_enable[1] = GX_FALSE;
    g_gx.chan_ctrl_amb_src[0] = GX_SRC_REG;
    g_gx.chan_ctrl_amb_src[1] = GX_SRC_REG;
    g_gx.chan_ctrl_mat_src[0] = GX_SRC_REG;
    g_gx.chan_ctrl_mat_src[1] = GX_SRC_REG;
    g_gx.chan_ctrl_light_mask[0] = 0;
    g_gx.chan_ctrl_light_mask[1] = 0;
    g_gx.chan_ctrl_diff_fn[0] = GX_DF_NONE;
    g_gx.chan_ctrl_diff_fn[1] = GX_DF_NONE;
    g_gx.chan_ctrl_attn_fn[0] = GX_AF_NONE;
    g_gx.chan_ctrl_attn_fn[1] = GX_AF_NONE;
    for (component = 0; component < 4; component++) {
        g_gx.chan_amb_color[0][component] = 0.0f;
        g_gx.chan_mat_color[0][component] = 1.0f;
    }

    GXSetTexCoordGen2(
        GX_TEXCOORD0,
        GX_TG_MTX2x4,
        GX_TG_TEX0,
        GX_IDENTITY,
        GX_FALSE,
        GX_PTIDENTITY
    );
    g_gx.gl_textures[0] = (GLuint)17;
    g_gx.tex_obj_w[0] = 4;
    g_gx.tex_obj_h[0] = 4;
    g_gx.tex_obj_fmt[0] = GX_TF_RGBA8;

    g_gx.tev_swap_table[0].r = 0;
    g_gx.tev_swap_table[0].g = 1;
    g_gx.tev_swap_table[0].b = 2;
    g_gx.tev_swap_table[0].a = 3;

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
    stage->tex_coord = GX_TEXCOORD0;
    stage->tex_map = GX_TEXMAP0;
    stage->color_chan = GX_COLOR0A0;
    stage->k_color_sel = GX_TEV_KCSEL_1_4;
    stage->k_alpha_sel = GX_TEV_KASEL_1;
    stage->ras_swap = GX_TEV_SWAP0;
    stage->tex_swap = GX_TEV_SWAP0;

    for (index = 0; index < 3; index++) {
        g_gx.vertex_buffer[index].position[0] = (float)index;
        g_gx.vertex_buffer[index].position[1] = (float)(index + 1);
        g_gx.vertex_buffer[index].position[2] = (float)(index + 2);
        g_gx.vertex_buffer[index].normal[1] = 1.0f;
        g_gx.vertex_buffer[index].color0[0] = (unsigned char)(index + 1);
        g_gx.vertex_buffer[index].color0[1] = (unsigned char)(index + 2);
        g_gx.vertex_buffer[index].color0[2] = (unsigned char)(index + 3);
        g_gx.vertex_buffer[index].color0[3] = 0xFF;
        g_gx.vertex_buffer[index].texcoord[0][0] = 0.25f * (float)index;
        g_gx.vertex_buffer[index].texcoord[0][1] = 0.5f * (float)index;
    }
}

static int check_state_reason(const char* expected_reason) {
    AcgcGxSemanticPacketV2 packet;
    int expected_supported = strcmp(expected_reason, "supported") == 0;

    CHECK(strcmp(
        pc_gx_semantic_v2_state_rejection_reason_fixture(),
        expected_reason
    ) == 0);
    CHECK(pc_gx_semantic_v2_state_is_supported_fixture() ==
        expected_supported);
    memset(&packet, 0xA5, sizeof(packet));
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, &packet) ==
        expected_supported);
    return 0;
}

int main(void) {
    AcgcGxSemanticPacketV2 packet;

    set_supported_state();
    CHECK(check_state_reason("supported") == 0);
    CHECK(strcmp(
        pc_gx_semantic_v2_rejection_reason_fixture(0, 3, 3),
        "supported"
    ) == 0);

    /* Vertex/count guards precede state classification for both handoff forms. */
    CHECK(strcmp(
        pc_gx_semantic_v2_rejection_reason_fixture(-1, 3, 3),
        "vertex_or_count"
    ) == 0);
    CHECK(strcmp(
        pc_gx_semantic_v2_rejection_reason_fixture(0, 4, 4),
        "vertex_or_count"
    ) == 0);
    CHECK(strcmp(
        pc_gx_semantic_v2_rejection_reason_fixture(0, 3, 6),
        "vertex_or_count"
    ) == 0);
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 4, &packet) == 0);
    CHECK(pc_gx_build_semantic_packet_v2_fixture(0, 3, NULL) == 0);

    /* Each case also arms later failures to prove the classifier's precedence. */
    set_supported_state();
    g_gx.num_chans = 0;
    g_gx.alpha_comp0 = GX_NEVER;
    g_gx.blend_src = GX_BL_SRCALPHA;
    CHECK(check_state_reason("global_count") == 0);

    set_supported_state();
    g_gx.alpha_comp0 = GX_NEVER;
    g_gx.blend_src = GX_BL_SRCALPHA;
    g_gx.z_compare_enable = GX_FALSE;
    CHECK(check_state_reason("alpha_test") == 0);

    set_supported_state();
    g_gx.blend_src = GX_BL_SRCALPHA;
    g_gx.blend_dst = GX_BL_INVSRCALPHA;
    g_gx.z_compare_enable = GX_FALSE;
    CHECK(check_state_reason("blend") == 0);

    set_supported_state();
    g_gx.z_compare_enable = GX_FALSE;
    g_gx.z_update_enable = GX_FALSE;
    g_gx.color_update_enable = GX_FALSE;
    CHECK(check_state_reason("depth") == 0);

    set_supported_state();
    g_gx.color_update_enable = GX_FALSE;
    g_gx.alpha_update_enable = GX_FALSE;
    g_gx.cull_mode = GX_CULL_BACK;
    CHECK(check_state_reason("color_alpha_update") == 0);

    set_supported_state();
    g_gx.cull_mode = GX_CULL_BACK;
    g_gx.current_mtx = 10;
    CHECK(check_state_reason("cull") == 0);

    set_supported_state();
    g_gx.current_mtx = 10;
    g_gx.chan_ctrl_mat_src[0] = GX_SRC_VTX;
    CHECK(check_state_reason("matrix_projection") == 0);

    set_supported_state();
    g_gx.chan_ctrl_mat_src[0] = GX_SRC_VTX;
    g_gx.gl_textures[0] = 0;
    CHECK(check_state_reason("channel") == 0);

    set_supported_state();
    g_gx.gl_textures[0] = 0;
    g_gx.tex_gen_src[0] = GX_TG_TEX1;
    CHECK(check_state_reason("stage_or_texture") == 0);

    set_supported_state();
    g_gx.tex_gen_src[0] = GX_TG_TEX1;
    CHECK(check_state_reason("texgen") == 0);

    puts("pc GX V2 rejection-reason fixture: PASS (precedence, fail-closed parity, and supported state)");
    puts("proof boundary: bounded CPU predicate diagnostics only; no consumer, Metal, frame, pixel, or playability claim");
    return 0;
}
