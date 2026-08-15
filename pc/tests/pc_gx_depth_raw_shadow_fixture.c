#include "pc_gx_internal.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Widened PC implementation boundary lets this CPU fixture exercise the
 * fail-closed path for values that a typed GXBool caller cannot produce. */
extern void GXSetZMode(u32 compare_enable, u32 func, u32 update_enable);

/* The focused target links pc_gx.c without the full PC host executable. */
int g_pc_window_w = PC_SCREEN_WIDTH;
int g_pc_window_h = PC_SCREEN_HEIGHT;
int g_pc_widescreen_stretch = 0;

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

static const PCGXRawDepth* raw_depth(void) {
    return pc_gx_raw_depth_shadow_fixture();
}

static void reset_state(void) {
    /* Match pc_gx_init(): host defaults remain useful to legacy rendering,
     * while the raw setter-owned shadow starts deliberately unknown. */
    memset(&g_gx, 0, sizeof(g_gx));
    g_gx.z_compare_enable = GX_TRUE;
    g_gx.z_compare_func = GX_LEQUAL;
    g_gx.z_update_enable = GX_TRUE;
}

static int raw_depth_is_zero(void) {
    PCGXRawDepth zero;

    memset(&zero, 0, sizeof(zero));
    return memcmp(raw_depth(), &zero, sizeof(zero)) == 0;
}

static int expect_raw_depth(
    uint32_t compare_enable,
    uint32_t compare_func,
    uint32_t update_enable
) {
    const PCGXRawDepth* shadow = raw_depth();

    return shadow->known == 1 &&
        shadow->compare_enable == compare_enable &&
        shadow->compare_func == compare_func &&
        shadow->update_enable == update_enable &&
        shadow->reserved[0] == 0 &&
        shadow->reserved[1] == 0 &&
        shadow->reserved[2] == 0;
}

static int test_initial_unknownness(void) {
    reset_state();

    CHECK(raw_depth() == &g_gx.raw_depth);
    CHECK(raw_depth_is_zero());
    CHECK(g_gx.z_compare_enable == GX_TRUE);
    CHECK(g_gx.z_compare_func == GX_LEQUAL);
    CHECK(g_gx.z_update_enable == GX_TRUE);
    return 0;
}

static int test_all_valid_triples(void) {
    uint32_t compare_enable;
    uint32_t compare_func;
    uint32_t update_enable;

    for (compare_enable = 0; compare_enable <= 1; compare_enable++) {
        for (update_enable = 0; update_enable <= 1; update_enable++) {
            for (compare_func = 0;
                 compare_func <= (uint32_t)GX_ALWAYS;
                 compare_func++) {
                reset_state();
                GXSetZMode(compare_enable, compare_func, update_enable);
                CHECK(expect_raw_depth(
                    compare_enable,
                    compare_func,
                    update_enable
                ));
                CHECK(g_gx.z_compare_enable == (int)compare_enable);
                CHECK(g_gx.z_compare_func == (int)compare_func);
                CHECK(g_gx.z_update_enable == (int)update_enable);
                /* Disabled comparison retains the exact logical function. */
                if (compare_enable == 0) {
                    CHECK(raw_depth()->compare_func == compare_func);
                }
            }
        }
    }
    return 0;
}

static int test_early_return_and_repeat(void) {
    uint32_t dirty_before;

    reset_state();
    dirty_before = g_gx.dirty;

    /* Equals the legacy host default: raw provenance must be known before the
     * existing equality return, without manufacturing a dirty transition. */
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    CHECK(expect_raw_depth(GX_TRUE, GX_LEQUAL, GX_TRUE));
    CHECK(g_gx.dirty == dirty_before);

    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    CHECK(expect_raw_depth(GX_TRUE, GX_LEQUAL, GX_TRUE));
    CHECK(g_gx.dirty == dirty_before);
    return 0;
}

static int test_malformed_values_fail_closed(void) {
    uint32_t dirty_before;

    reset_state();
    GXSetZMode(GX_TRUE, GX_GREATER, GX_TRUE);
    CHECK(expect_raw_depth(GX_TRUE, GX_GREATER, GX_TRUE));

    GXSetZMode(2, GX_GREATER, GX_TRUE);
    CHECK(raw_depth_is_zero());
    CHECK(g_gx.z_compare_enable == 2);

    GXSetZMode(GX_TRUE, 8, GX_TRUE);
    CHECK(raw_depth_is_zero());
    CHECK(g_gx.z_compare_func == 8);

    GXSetZMode(GX_TRUE, GX_GREATER, 2);
    CHECK(raw_depth_is_zero());
    CHECK(g_gx.z_update_enable == 2);

    /* The malformed triple also fails closed when the legacy host state makes
     * the existing equality path return immediately. */
    dirty_before = g_gx.dirty;
    GXSetZMode(GX_TRUE, GX_GREATER, 2);
    CHECK(raw_depth_is_zero());
    CHECK(g_gx.dirty == dirty_before);

    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    CHECK(expect_raw_depth(GX_FALSE, GX_ALWAYS, GX_FALSE));
    return 0;
}

static int test_other_raw_shadows_untouched(void) {
    PCGXRawTransform transform_before;
    PCGXTevRawColor tev_before[4];
    PCGXTevRawColor kcolor_before[4];

    reset_state();
    memset(&g_gx.raw_transform, 0xA5, sizeof(g_gx.raw_transform));
    memset(g_gx.tev_raw_colors, 0x5A, sizeof(g_gx.tev_raw_colors));
    memset(g_gx.tev_raw_k_colors, 0x3C, sizeof(g_gx.tev_raw_k_colors));
    memcpy(&transform_before, &g_gx.raw_transform, sizeof(transform_before));
    memcpy(tev_before, g_gx.tev_raw_colors, sizeof(tev_before));
    memcpy(kcolor_before, g_gx.tev_raw_k_colors, sizeof(kcolor_before));

    GXSetZMode(GX_TRUE, GX_EQUAL, GX_FALSE);
    CHECK(memcmp(
        &transform_before,
        &g_gx.raw_transform,
        sizeof(transform_before)
    ) == 0);
    CHECK(memcmp(tev_before, g_gx.tev_raw_colors, sizeof(tev_before)) == 0);
    CHECK(memcmp(
        kcolor_before,
        g_gx.tev_raw_k_colors,
        sizeof(kcolor_before)
    ) == 0);
    return 0;
}

int main(void) {
    if (test_initial_unknownness() != 0 ||
        test_all_valid_triples() != 0 ||
        test_early_return_and_repeat() != 0 ||
        test_malformed_values_fail_closed() != 0 ||
        test_other_raw_shadows_untouched() != 0) {
        return 1;
    }

    puts("pc GX raw Depth shadow fixture: PASS");
    puts("proof boundary: setter-owned CPU Depth provenance and existing raw Transform/TEV shadow isolation only; no canonical producer, renderer, Metal, or playability claim");
    return 0;
}
