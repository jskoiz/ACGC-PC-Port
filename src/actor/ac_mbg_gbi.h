#ifndef AC_MBG_GBI_H
#define AC_MBG_GBI_H

#include <libforest/gbi_extensions.h>

enum {
    ACGC_MBG_MODEL_GFX_COUNT = 12,
};

/*
 * Keep the model's command order and words identical to the original static
 * list while allowing the asset-backed Vtx pointer to use the TARGET_PC
 * runtime reference path on LP64 hosts. The pointer-free commands stay in a
 * static word template so the legacy GBI shift macros are not re-evaluated at
 * runtime under sanitizer instrumentation.
 */
static const Gfx ac_mbg_model_template[ACGC_MBG_MODEL_GFX_COUNT] = {
    gsDPPipeSync(),
    gsDPSetRenderMode(G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2),
    gsDPSetCombineLERP(PRIMITIVE, 0, SHADE, 0, 0, 0, 0, 1, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED),
    gsDPSetPrimColor(0, 128, 255, 255, 0, 255),
    gsSPLoadGeometryMode(G_ZBUFFER | G_SHADE | G_FOG | G_LIGHTING | G_SHADING_SMOOTH),
    {{ _SHIFTL(G_VTX, 24, 8) | _SHIFTL(8, 12, 8) | _SHIFTL(8, 1, 7), 0 }},
    gsSP2Triangles(5, 6, 7, 0, 4, 5, 7, 0),
    gsSP2Triangles(7, 6, 2, 0, 7, 2, 3, 0),
    gsSP2Triangles(5, 1, 6, 0, 6, 1, 2, 0),
    gsSP2Triangles(4, 0, 5, 0, 5, 0, 1, 0),
    gsSP2Triangles(4, 7, 0, 0, 0, 7, 3, 0),
    gsSPEndDisplayList(),
};

static inline void ac_mbg_build_model(Gfx* model, const Vtx* vertices) {
    int command;

    for (command = 0; command < ACGC_MBG_MODEL_GFX_COUNT; command++) {
        model[command] = ac_mbg_model_template[command];
    }
    gSPVertex(model + 5, vertices, 8, 0);
}

#endif /* AC_MBG_GBI_H */
