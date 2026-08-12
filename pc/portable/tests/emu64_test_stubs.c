#include <stdint.h>

typedef struct {
    float x;
    float y;
    float z;
} AcgcTestVec;

typedef float AcgcTestMtx[3][4];

unsigned int pc_image_base = 0;
unsigned int pc_image_end = 0;
int g_pc_verbose = 0;
int g_pc_widescreen_stretch = 0;
int g_pc_window_w = 640;
int g_pc_window_h = 480;
int pc_emu64_frame_cmds = 0;
int pc_emu64_frame_noop_cmds = 0;
int pc_emu64_frame_tri_cmds = 0;
int pc_emu64_frame_vtx_cmds = 0;
int pc_emu64_frame_dl_cmds = 0;
int pc_emu64_frame_cull_visible = 0;
int pc_emu64_frame_cull_rejected = 0;
unsigned short s_tlut_first_word[16] = { 0 };
unsigned char GXNtsc480IntDf[256] = { 0 };

void PSMTXMultVec(const AcgcTestMtx matrix, const AcgcTestVec* source, AcgcTestVec* destination) {
    (void)matrix;
    *destination = *source;
}

void PSVECNormalize(const AcgcTestVec* source, AcgcTestVec* destination) {
    *destination = *source;
}

#define ACGC_EMU64_VOID_STUB(name) void name(void) {}

ACGC_EMU64_VOID_STUB(C_MTXLightOrtho)
ACGC_EMU64_VOID_STUB(C_MTXOrtho)
ACGC_EMU64_VOID_STUB(DCStoreRange)
ACGC_EMU64_VOID_STUB(GXBegin)
ACGC_EMU64_VOID_STUB(GXCallDisplayList)
ACGC_EMU64_VOID_STUB(GXClearVtxDesc)
ACGC_EMU64_VOID_STUB(GXColor4u8)
ACGC_EMU64_VOID_STUB(GXEnableTexOffsets)
ACGC_EMU64_VOID_STUB(GXEnd)
ACGC_EMU64_VOID_STUB(GXInitLightAttn)
ACGC_EMU64_VOID_STUB(GXInitLightColor)
ACGC_EMU64_VOID_STUB(GXInitLightDir)
ACGC_EMU64_VOID_STUB(GXInitLightPos)
ACGC_EMU64_VOID_STUB(GXInitTexObj)
ACGC_EMU64_VOID_STUB(GXInitTexObjCI)
ACGC_EMU64_VOID_STUB(GXInitTexObjLOD)
ACGC_EMU64_VOID_STUB(GXInitTlutObj)
ACGC_EMU64_VOID_STUB(GXInvalidateTexAll)
ACGC_EMU64_VOID_STUB(GXInvalidateVtxCache)
ACGC_EMU64_VOID_STUB(GXLoadLightObjImm)
ACGC_EMU64_VOID_STUB(GXLoadNrmMtxImm)
ACGC_EMU64_VOID_STUB(GXLoadPosMtxImm)
ACGC_EMU64_VOID_STUB(GXLoadTexMtxImm)
ACGC_EMU64_VOID_STUB(GXLoadTexObj)
ACGC_EMU64_VOID_STUB(GXLoadTlut)
ACGC_EMU64_VOID_STUB(GXNormal3f32)
ACGC_EMU64_VOID_STUB(GXPosition2f32)
ACGC_EMU64_VOID_STUB(GXPosition3f32)
ACGC_EMU64_VOID_STUB(GXSetAlphaCompare)
ACGC_EMU64_VOID_STUB(GXSetAlphaUpdate)
ACGC_EMU64_VOID_STUB(GXSetBlendMode)
ACGC_EMU64_VOID_STUB(GXSetChanAmbColor)
ACGC_EMU64_VOID_STUB(GXSetChanCtrl)
ACGC_EMU64_VOID_STUB(GXSetChanMatColor)
ACGC_EMU64_VOID_STUB(GXSetClipMode)
ACGC_EMU64_VOID_STUB(GXSetCoPlanar)
ACGC_EMU64_VOID_STUB(GXSetColorUpdate)
ACGC_EMU64_VOID_STUB(GXSetCullMode)
ACGC_EMU64_VOID_STUB(GXSetCurrentGXThread)
ACGC_EMU64_VOID_STUB(GXSetCurrentMtx)
ACGC_EMU64_VOID_STUB(GXSetDither)
ACGC_EMU64_VOID_STUB(GXSetDstAlpha)
ACGC_EMU64_VOID_STUB(GXSetFieldMask)
ACGC_EMU64_VOID_STUB(GXSetFieldMode)
ACGC_EMU64_VOID_STUB(GXSetFog)
ACGC_EMU64_VOID_STUB(GXSetFogRangeAdj)
ACGC_EMU64_VOID_STUB(GXSetIndTexCoordScale)
ACGC_EMU64_VOID_STUB(GXSetLineWidth)
ACGC_EMU64_VOID_STUB(GXSetNumChans)
ACGC_EMU64_VOID_STUB(GXSetNumIndStages)
ACGC_EMU64_VOID_STUB(GXSetNumTevStages)
ACGC_EMU64_VOID_STUB(GXSetNumTexGens)
ACGC_EMU64_VOID_STUB(GXSetPixelFmt)
ACGC_EMU64_VOID_STUB(GXSetProjection)
ACGC_EMU64_VOID_STUB(GXSetScissor)
ACGC_EMU64_VOID_STUB(GXSetScissorBoxOffset)
ACGC_EMU64_VOID_STUB(GXSetTevAlphaIn)
ACGC_EMU64_VOID_STUB(GXSetTevAlphaOp)
ACGC_EMU64_VOID_STUB(GXSetTevColor)
ACGC_EMU64_VOID_STUB(GXSetTevColorIn)
ACGC_EMU64_VOID_STUB(GXSetTevColorOp)
ACGC_EMU64_VOID_STUB(GXSetTevDirect)
ACGC_EMU64_VOID_STUB(GXSetTevKAlphaSel)
ACGC_EMU64_VOID_STUB(GXSetTevKColorSel)
ACGC_EMU64_VOID_STUB(GXSetTevOp)
ACGC_EMU64_VOID_STUB(GXSetTevOrder)
ACGC_EMU64_VOID_STUB(GXSetTevSwapMode)
ACGC_EMU64_VOID_STUB(GXSetTevSwapModeTable)
ACGC_EMU64_VOID_STUB(GXSetTexCoordGen2)
ACGC_EMU64_VOID_STUB(GXSetViewport)
ACGC_EMU64_VOID_STUB(GXSetVtxAttrFmt)
ACGC_EMU64_VOID_STUB(GXSetVtxDesc)
ACGC_EMU64_VOID_STUB(GXSetZCompLoc)
ACGC_EMU64_VOID_STUB(GXSetZMode)
ACGC_EMU64_VOID_STUB(GXSetZTexture)
ACGC_EMU64_VOID_STUB(GXTexCoord2f32)
ACGC_EMU64_VOID_STUB(GXTexCoord2s16)
ACGC_EMU64_VOID_STUB(PPCSync)
ACGC_EMU64_VOID_STUB(PSMTXConcat)
ACGC_EMU64_VOID_STUB(PSMTXCopy)
ACGC_EMU64_VOID_STUB(PSMTXIdentity)
ACGC_EMU64_VOID_STUB(PSMTXInverse)
ACGC_EMU64_VOID_STUB(PSMTXScale)
ACGC_EMU64_VOID_STUB(PSMTXTrans)
ACGC_EMU64_VOID_STUB(pc_gx_tlut_set_native_le)

void JW_JUTReport(void) {}
void OSReport(void) {}
void convert_partial_address(void) {}
