#include "libforest/gbi_extensions.h"

#include <PR/mbi.h>

#include <cstdint>

static Vtx static_cpp_vertices[8] = {};
static Mtx static_cpp_matrix = {};
static u8 static_cpp_texture[128] = {};
static u16 static_cpp_palette[16] = {};
static u16 static_cpp_color_image[16] = {};
static u16 static_cpp_depth_image[16] = {};
static Gfx static_cpp_nested[] = {
    gsSPEndDisplayList(),
};

static const Gfx static_cpp_commands[] = {
    gsSPVertex(static_cpp_vertices, 8, 0),
    gsSPMatrix(&static_cpp_matrix, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH),
    gsMoveWd(G_MW_SEGMENT, G_MWO_SEGMENT_A, SEGMENT_ADDR(G_MWO_SEGMENT_A, 0)),
    gsSPPopMatrixN(0, 0),
    gsDPSetColorImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, static_cpp_color_image),
    gsDPSetDepthImage(static_cpp_depth_image),
    gsDPSetTextureImage(G_IM_FMT_CI, G_IM_SIZ_8b, 4, static_cpp_texture),
    gsDPSetTextureImage_Dolphin(G_IM_FMT_CI, G_IM_SIZ_4b, 16, 32, static_cpp_texture),
    gsDPLoadTLUT_Dolphin(15, 16, 1, static_cpp_palette),
    gsSPDisplayList(static_cpp_nested),
    gsSPDisplayList(SEGMENT_ADDR(G_MWO_SEGMENT_A, 0)),
    gsSPNoOp(),
    gsSPEndDisplayList(),
};

static_assert(sizeof(Gfx) == 8, "Gfx must remain exactly 8 bytes");

extern "C" int acgc_static_gbi_compile_cpp_probe() {
    return static_cpp_commands[0].words.w0 == 0 && static_cpp_nested[0].words.w0 == 0;
}
