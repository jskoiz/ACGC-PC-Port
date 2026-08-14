#include "pc_gx_internal.h"
#include "acgc/gx_semantic_packet.h"

#include <dolphin/gx/GXEnum.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void (*V2HandoffCallback)(
    void* context,
    const AcgcGxSemanticPacketV2* packet
);

extern void pc_gx_set_semantic_packet_v2_handoff(
    V2HandoffCallback callback,
    void* context
);
extern void pc_gx_clear_semantic_packet_v2_handoff(void);
extern int pc_gx_try_handoff_semantic_packet_v2(
    int first_vertex,
    int vertex_count
);
extern void GXSetTexCoordGen2(
    u32 dst,
    u32 func,
    u32 src,
    u32 mtx,
    GXBool normalize,
    u32 postmtx
);

static PCGXShaderVariant s_fixture_variant;

void pc_gx_tev_seq_reset(void) {
}

PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    return &s_fixture_variant;
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static uint32_t float_bits(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

typedef struct V2Probe {
    unsigned int calls;
    int valid;
    uint32_t first_positions[42];
} V2Probe;

static void record_v2_packet(
    void* context,
    const AcgcGxSemanticPacketV2* packet
) {
    V2Probe* probe = (V2Probe*)context;

    if (probe == NULL || packet == NULL ||
        !acgc_gx_semantic_packet_v2_validate(packet) ||
        packet->base.primitive != ACGC_GX_SEMANTIC_PRIMITIVE_TRIANGLES ||
        packet->base.vertex_count != 3) {
        if (probe != NULL) {
            probe->valid = 0;
        }
        return;
    }
    if (probe->calls >= sizeof(probe->first_positions) /
            sizeof(probe->first_positions[0])) {
        probe->valid = 0;
        return;
    }
    probe->first_positions[probe->calls] =
        packet->base.vertices[0].position[0];
    probe->calls++;
}

static void reset_probe(V2Probe* probe) {
    memset(probe, 0, sizeof(*probe));
    probe->valid = 1;
}

static void set_v2_state(int primitive, int first_vertex, int vertex_count) {
    int index;
    int component;

    memset(&g_gx, 0, sizeof(g_gx));
    memset(&s_fixture_variant, 0, sizeof(s_fixture_variant));

    g_gx.current_primitive = primitive;
    g_gx.current_mtx = 0;
    g_gx.expected_vertex_count = vertex_count;
    g_gx.current_vertex_idx = first_vertex + vertex_count;
    g_gx.pending_verts = first_vertex;
    g_gx.pending_prim = primitive;
    g_gx.num_chans = 1;
    g_gx.num_tex_gens = 2;
    g_gx.num_tev_stages = 2;
    g_gx.projection_type = GX_PERSPECTIVE;
    g_gx.fog_type = GX_FOG_NONE;
    g_gx.alpha_comp0 = GX_ALWAYS;
    g_gx.alpha_comp1 = GX_ALWAYS;
    g_gx.alpha_op = GX_AOP_AND;
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

    for (index = 0; index < vertex_count; index++) {
        PCGXVertex* vertex = &g_gx.vertex_buffer[first_vertex + index];

        vertex->position[0] = (float)(100 + first_vertex + index);
        vertex->position[1] = (float)(200 + first_vertex + index);
        vertex->position[2] = (float)(300 + first_vertex + index);
        vertex->normal[1] = 1.0f;
        vertex->color0[0] = (unsigned char)(index + 1);
        vertex->color0[1] = (unsigned char)(index + 2);
        vertex->color0[2] = (unsigned char)(index + 3);
        vertex->color0[3] = 0xFF;
        vertex->texcoord[0][0] = (float)index / 10.0f;
        vertex->texcoord[0][1] = (float)index / 20.0f;
    }

    /* The no-context fixture takes the deferred-run absorption path after the
     * optional handoff, so it never reaches a GL call. */
    g_gx.dirty = 0;
    g_gx.current_shader = 0;
}

int main(void) {
    V2Probe probe;

    reset_probe(&probe);
    set_v2_state(GX_TRIANGLES, 0, 3);
    pc_gx_set_semantic_packet_v2_handoff(record_v2_packet, &probe);
    CHECK(pc_gx_try_handoff_semantic_packet_v2(0, 3) == 1);
    CHECK(probe.valid == 1);
    CHECK(probe.calls == 1);
    CHECK(probe.first_positions[0] == float_bits(100.0f));

    reset_probe(&probe);
    set_v2_state(GX_TRIANGLES, 0, 4);
    CHECK(pc_gx_try_handoff_semantic_packet_v2(0, 4) == 0);
    CHECK(probe.calls == 0);

    reset_probe(&probe);
    set_v2_state(GX_TRIANGLES, 1, 6);
    pc_gx_flush_vertices();
    CHECK(probe.valid == 1);
    CHECK(probe.calls == 2);
    CHECK(probe.first_positions[0] == float_bits(101.0f));
    CHECK(probe.first_positions[1] == float_bits(104.0f));

    reset_probe(&probe);
    set_v2_state(GX_TRIANGLES, 1, 9);
    pc_gx_flush_vertices();
    CHECK(probe.valid == 1);
    CHECK(probe.calls == 3);
    CHECK(probe.first_positions[0] == float_bits(101.0f));
    CHECK(probe.first_positions[1] == float_bits(104.0f));
    CHECK(probe.first_positions[2] == float_bits(107.0f));

    reset_probe(&probe);
    set_v2_state(GX_TRIANGLES, 1, 4);
    pc_gx_flush_vertices();
    CHECK(probe.calls == 0);

    reset_probe(&probe);
    set_v2_state(GX_QUADS, 1, 4);
    pc_gx_flush_vertices();
    CHECK(probe.calls == 0);

    reset_probe(&probe);
    set_v2_state(GX_TRIANGLES, 1, 6);
    pc_gx_clear_semantic_packet_v2_handoff();
    pc_gx_flush_vertices();
    CHECK(probe.calls == 0);
    pc_gx_set_semantic_packet_v2_handoff(record_v2_packet, &probe);

    reset_probe(&probe);
    set_v2_state(GX_TRIANGLES, 1, 6);
    g_gx.fog_type = GX_FOG_PERSP_LIN;
    pc_gx_flush_vertices();
    CHECK(probe.calls == 0);

    reset_probe(&probe);
    set_v2_state(GX_TRIANGLES, 1, 9);
    g_gx.vertex_buffer[7].position[0] = NAN;
    pc_gx_flush_vertices();
    CHECK(probe.calls == 0);

    reset_probe(&probe);
    set_v2_state(GX_TRIANGLES, 1, 6);
    g_gx.pending_verts = PC_GX_MAX_VERTS - 2;
    g_gx.current_vertex_idx = g_gx.pending_verts + 6;
    pc_gx_flush_vertices();
    CHECK(probe.calls == 0);

    pc_gx_clear_semantic_packet_v2_handoff();
    puts("pc GX semantic v2 batch handoff tests: PASS (exact-three compatibility, 6/9 slicing, and all-or-nothing rejection)");
    puts("proof boundary: synchronous CPU packet handoff only; legacy GL remains the submission path; no Metal frame, device, pixel, or playability claim");
    return 0;
}
