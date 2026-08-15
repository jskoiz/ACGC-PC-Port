#include "pc_gx_internal.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void GXSetTevColor(u32 id, u32 color_packed);
extern void GXSetTevColorS10(u32 id, s16 r, s16 g, s16 b, s16 a);
extern void GXSetTevKColor(u32 id, u32 color_packed);

/* Keep this fixture CPU-only; the real TEV shader module is outside this lane. */
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

static void reset_state(void) {
    /* This is the state reset performed at the top of pc_gx_init(). */
    memset(&g_gx, 0, sizeof(g_gx));
}

static int expect_raw(
    const PCGXTevRawColor* shadow,
    const int32_t expected[4],
    int valid,
    PCGXTevRawSource source
) {
    int component;

    if (shadow->valid != (uint8_t)(valid ? 1 : 0) ||
        shadow->source != (uint8_t)source) {
        return 0;
    }
    for (component = 0; component < 4; component++) {
        if (shadow->components[component] != expected[component]) {
            return 0;
        }
    }
    return 1;
}

static int expect_float4(const float actual[4], const int32_t expected[4]) {
    int component;

    for (component = 0; component < 4; component++) {
        if (actual[component] != (float)expected[component] / 255.0f) {
            return 0;
        }
    }
    return 1;
}

static int expect_unavailable(void) {
    static const int32_t zero[4] = {0, 0, 0, 0};
    int index;

    for (index = 0; index < 4; index++) {
        if (!expect_raw(
                &g_gx.tev_raw_colors[index],
                zero,
                0,
                PCGX_TEV_RAW_SOURCE_UNAVAILABLE) ||
            !expect_raw(
                &g_gx.tev_raw_k_colors[index],
                zero,
                0,
                PCGX_TEV_RAW_SOURCE_UNAVAILABLE)) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    static const int32_t zero[4] = {0, 0, 0, 0};
    static const int32_t u8_one_two_three_four[4] = {1, 2, 3, 4};
    static const int32_t u8_five_six_seven_eight[4] = {5, 6, 7, 8};
    static const int32_t u8_max[4] = {255, 255, 255, 255};
    static const int32_t s10_endpoints[4] = {-1024, 1023, 0, -1};
    static const int32_t konst0[4] = {0x11, 0x22, 0x33, 0x44};
    static const int32_t konst1[4] = {0x55, 0x66, 0x77, 0x88};
    static const int32_t konst2[4] = {0x99, 0xAA, 0xBB, 0xCC};
    static const int32_t konst3[4] = {0xDD, 0xEE, 0xFF, 0x00};
    PCGXTevRawColor registers_before[4];
    PCGXTevRawColor konst_before[4];
    float float_before[4];
    float same_float_before[4];
    uint32_t dirty_before;

    reset_state();
    CHECK(expect_unavailable());

    /* Zero is a valid setter-owned u8 value, not the reset/unavailable state. */
    GXSetTevColor(GX_TEVPREV, 0);
    CHECK(expect_raw(
        &g_gx.tev_raw_colors[GX_TEVPREV],
        zero,
        1,
        PCGX_TEV_RAW_SOURCE_COLOR_U8
    ));
    CHECK(expect_float4(g_gx.tev_colors[GX_TEVPREV], zero));

    /* The PC path's two packed-color conventions remain unchanged: REG0 is
     * passed as GXColor bytes, while the other slots receive EmuColor.raw. */
    GXSetTevColor(GX_TEVREG0, UINT32_C(0x08070605));
    CHECK(expect_raw(
        &g_gx.tev_raw_colors[GX_TEVREG0],
        u8_five_six_seven_eight,
        1,
        PCGX_TEV_RAW_SOURCE_COLOR_U8
    ));
    CHECK(expect_float4(g_gx.tev_colors[GX_TEVREG0], u8_five_six_seven_eight));

    GXSetTevColor(GX_TEVPREV, UINT32_C(0x01020304));
    CHECK(expect_raw(
        &g_gx.tev_raw_colors[GX_TEVPREV],
        u8_one_two_three_four,
        1,
        PCGX_TEV_RAW_SOURCE_COLOR_U8
    ));
    CHECK(expect_float4(g_gx.tev_colors[GX_TEVPREV], u8_one_two_three_four));

    GXSetTevColor(GX_TEVREG0, UINT32_C(0xFFFFFFFF));
    CHECK(expect_raw(
        &g_gx.tev_raw_colors[GX_TEVREG0],
        u8_max,
        1,
        PCGX_TEV_RAW_SOURCE_COLOR_U8
    ));
    CHECK(expect_float4(g_gx.tev_colors[GX_TEVREG0], u8_max));

    /* Raw provenance changes even when the normalized float path returns. */
    reset_state();
    GXSetTevColor(GX_TEVPREV, 0);
    memcpy(same_float_before, g_gx.tev_colors[GX_TEVPREV],
           sizeof(same_float_before));
    dirty_before = g_gx.dirty;
    GXSetTevColorS10(GX_TEVPREV, 0, 0, 0, 0);
    CHECK(expect_raw(
        &g_gx.tev_raw_colors[GX_TEVPREV],
        zero,
        1,
        PCGX_TEV_RAW_SOURCE_COLOR_S10
    ));
    CHECK(memcmp(
        same_float_before,
        g_gx.tev_colors[GX_TEVPREV],
        sizeof(same_float_before)
    ) == 0);
    CHECK(g_gx.dirty == dirty_before);
    GXSetTevColor(GX_TEVPREV, 0);
    CHECK(expect_raw(
        &g_gx.tev_raw_colors[GX_TEVPREV],
        zero,
        1,
        PCGX_TEV_RAW_SOURCE_COLOR_U8
    ));
    CHECK(memcmp(
        same_float_before,
        g_gx.tev_colors[GX_TEVPREV],
        sizeof(same_float_before)
    ) == 0);
    CHECK(g_gx.dirty == dirty_before);

    /* Both setter orders overwrite the same register's raw provenance. */
    GXSetTevColorS10(GX_TEVPREV, -1024, 1023, 0, -1);
    CHECK(expect_raw(
        &g_gx.tev_raw_colors[GX_TEVPREV],
        s10_endpoints,
        1,
        PCGX_TEV_RAW_SOURCE_COLOR_S10
    ));
    CHECK(expect_float4(g_gx.tev_colors[GX_TEVPREV], s10_endpoints));

    GXSetTevColorS10(GX_TEVREG0, -1024, 1023, 0, -1);
    CHECK(expect_raw(
        &g_gx.tev_raw_colors[GX_TEVREG0],
        s10_endpoints,
        1,
        PCGX_TEV_RAW_SOURCE_COLOR_S10
    ));
    CHECK(expect_float4(g_gx.tev_colors[GX_TEVREG0], s10_endpoints));

    GXSetTevColor(GX_TEVPREV, UINT32_C(0x05060708));
    CHECK(expect_raw(
        &g_gx.tev_raw_colors[GX_TEVPREV],
        u8_five_six_seven_eight,
        1,
        PCGX_TEV_RAW_SOURCE_COLOR_U8
    ));
    CHECK(expect_float4(g_gx.tev_colors[GX_TEVPREV], u8_five_six_seven_eight));

    GXSetTevColorS10(GX_TEVREG1, -1024, 1023, 0, -1);
    CHECK(expect_raw(
        &g_gx.tev_raw_colors[GX_TEVREG1],
        s10_endpoints,
        1,
        PCGX_TEV_RAW_SOURCE_COLOR_S10
    ));
    CHECK(expect_float4(g_gx.tev_colors[GX_TEVREG1], s10_endpoints));
    GXSetTevColorS10(GX_TEVREG2, -1024, 1023, 0, -1);
    CHECK(expect_raw(
        &g_gx.tev_raw_colors[GX_TEVREG2],
        s10_endpoints,
        1,
        PCGX_TEV_RAW_SOURCE_COLOR_S10
    ));
    CHECK(expect_float4(g_gx.tev_colors[GX_TEVREG2], s10_endpoints));

    GXSetTevKColor(GX_KCOLOR0, UINT32_C(0x11223344));
    GXSetTevKColor(GX_KCOLOR1, UINT32_C(0x55667788));
    GXSetTevKColor(GX_KCOLOR2, UINT32_C(0x99AABBCC));
    GXSetTevKColor(GX_KCOLOR3, UINT32_C(0xDDEEFF00));
    CHECK(expect_raw(
        &g_gx.tev_raw_k_colors[GX_KCOLOR0], konst0, 1,
        PCGX_TEV_RAW_SOURCE_KCOLOR_U8
    ));
    CHECK(expect_raw(
        &g_gx.tev_raw_k_colors[GX_KCOLOR1], konst1, 1,
        PCGX_TEV_RAW_SOURCE_KCOLOR_U8
    ));
    CHECK(expect_raw(
        &g_gx.tev_raw_k_colors[GX_KCOLOR2], konst2, 1,
        PCGX_TEV_RAW_SOURCE_KCOLOR_U8
    ));
    CHECK(expect_raw(
        &g_gx.tev_raw_k_colors[GX_KCOLOR3], konst3, 1,
        PCGX_TEV_RAW_SOURCE_KCOLOR_U8
    ));
    CHECK(expect_float4(g_gx.tev_k_colors[GX_KCOLOR0], konst0));
    CHECK(expect_float4(g_gx.tev_k_colors[GX_KCOLOR3], konst3));

    /* Invalid IDs are no-ops for the existing state and cannot alter raw
     * validity or write beyond either four-entry shadow. */
    memcpy(registers_before, g_gx.tev_raw_colors, sizeof(registers_before));
    memcpy(konst_before, g_gx.tev_raw_k_colors, sizeof(konst_before));
    memcpy(float_before, g_gx.tev_colors[GX_TEVREG2], sizeof(float_before));
    GXSetTevColor(GX_MAX_TEVREG, UINT32_C(0x01020304));
    GXSetTevColorS10(GX_MAX_TEVREG, -1024, 1023, 0, -1);
    GXSetTevColor(UINT32_MAX, UINT32_C(0x01020304));
    GXSetTevKColor(GX_MAX_KCOLOR, UINT32_C(0x01020304));
    GXSetTevKColor(UINT32_MAX, UINT32_C(0x01020304));
    CHECK(memcmp(registers_before, g_gx.tev_raw_colors, sizeof(registers_before)) == 0);
    CHECK(memcmp(konst_before, g_gx.tev_raw_k_colors, sizeof(konst_before)) == 0);
    CHECK(memcmp(float_before, g_gx.tev_colors[GX_TEVREG2], sizeof(float_before)) == 0);

    /* A valid register ID with out-of-range S10 input is retained only as
     * malformed provenance; the current normalized float behavior remains. */
    GXSetTevColorS10(GX_TEVREG2, -1025, 1024, 0, 0);
    CHECK(g_gx.tev_raw_colors[GX_TEVREG2].valid == 0);
    CHECK(g_gx.tev_raw_colors[GX_TEVREG2].source ==
          PCGX_TEV_RAW_SOURCE_MALFORMED);
    CHECK(g_gx.tev_raw_colors[GX_TEVREG2].components[0] == -1025);
    CHECK(g_gx.tev_raw_colors[GX_TEVREG2].components[1] == 1024);
    CHECK(g_gx.tev_colors[GX_TEVREG2][0] == -1025 / 255.0f);
    CHECK(g_gx.tev_colors[GX_TEVREG2][1] == 1024 / 255.0f);

    GXSetTevColorS10(GX_TEVREG2, -1024, 1023, 0, -1);
    CHECK(expect_raw(
        &g_gx.tev_raw_colors[GX_TEVREG2],
        s10_endpoints,
        1,
        PCGX_TEV_RAW_SOURCE_COLOR_S10
    ));

    puts("pc GX raw TEV/KONST shadow fixture: PASS");
    puts("proof boundary: setter-owned CPU raw shadow and float-path preservation only; no canonical packet, renderer, Metal, or playability claim");
    return 0;
}
