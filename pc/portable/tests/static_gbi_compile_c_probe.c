#include <PR/mbi.h>
#include <libforest/gbi_extensions.h>

#include <stdint.h>

static Vtx static_probe_vertices[8];
static Mtx static_probe_matrix;
static u8 static_probe_texture[128];
static u16 static_probe_palette[16];
static u16 static_probe_color_image[16];
static u16 static_probe_depth_image[16];
static u8 static_probe_ucode_start[32];
static u8 static_probe_ucode_data[32];
static Gfx static_probe_nested[] = {
    gsSPEndDisplayList(),
};

/* Keep every pointer-capable static form in one representative translation
   unit. This is intentionally file-scope: the LP64 initializer must carry a
   real uintptr_t relocation in the adjacent union payload. */
static const Gfx static_probe_commands[] = {
    gsDma0p(G_MTX, &static_probe_matrix, sizeof(Mtx)),
    gsDma1p(G_DL, static_probe_nested, 0, G_DL_PUSH),
    gsDma2p(G_MTX, &static_probe_matrix, sizeof(Mtx), G_MTX_MODELVIEW, 0),
    gsSPVertex(static_probe_vertices, 8, 0),
    gsSPMatrix(&static_probe_matrix, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH),
    gsMoveWd(G_MW_SEGMENT, G_MWO_SEGMENT_A, SEGMENT_ADDR(G_MWO_SEGMENT_A, 0)),
    gsSPPopMatrixN(0, 0),
    gsSPBranchLessZraw(static_probe_nested, 0, UINT32_C(0x12345678)),
    gsSPLoadUcodeEx(static_probe_ucode_start, static_probe_ucode_data, sizeof(static_probe_ucode_data)),
    gsSPDmaRead(0, static_probe_ucode_data, sizeof(static_probe_ucode_data)),
    gsDPSetColorImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, static_probe_color_image),
    gsDPSetDepthImage(static_probe_depth_image),
    gsDPSetTextureImage(G_IM_FMT_CI, G_IM_SIZ_8b, 4, static_probe_texture),
    gsDPLoadTLUT_Dolphin(15, 16, 1, static_probe_palette),
    gsDPSetTextureImage_Dolphin(G_IM_FMT_CI, G_IM_SIZ_4b, 16, 32, static_probe_texture),
    gsDPLoadTextureBlock_4b_Dolphin(
        static_probe_texture, G_IM_FMT_CI, 16, 32, 15, GX_MIRROR, GX_CLAMP, 0, 0
    ),
    gsDPLoadMultiBlock_4b_Dolphin(
        static_probe_texture, 1, G_IM_FMT_CI, 16, 32, 15, GX_MIRROR, GX_CLAMP, 0, 0
    ),
    gsSPNoOp(),
    gsSPEndDisplayList(),
};

_Static_assert(sizeof(Gfx) == 8, "Gfx must remain exactly 8 bytes");

int acgc_static_gbi_compile_c_probe(void) {
    return static_probe_commands[0].words.w0 == 0 && static_probe_nested[0].words.w0 == 0;
}
