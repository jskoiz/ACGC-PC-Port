#include "acgc/renderer_fixtures.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static void store_u16(uint8_t* destination, uint16_t value, uint32_t byte_order) {
    if (byte_order == ACGC_RENDERER_FIXTURE_BIG_ENDIAN) {
        destination[0] = (uint8_t)(value >> 8);
        destination[1] = (uint8_t)value;
    } else {
        destination[0] = (uint8_t)value;
        destination[1] = (uint8_t)(value >> 8);
    }
}

static void set_texture_description(
    AcgcRendererFixtureTextureDescription* description,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t data_size
) {
    memset(description, 0, sizeof(*description));
    description->version = ACGC_RENDERER_FIXTURE_VERSION;
    description->width = width;
    description->height = height;
    description->format = format;
    description->data_byte_order = ACGC_RENDERER_FIXTURE_BIG_ENDIAN;
    description->data_size = data_size;
    description->tlut_byte_order = ACGC_RENDERER_FIXTURE_BIG_ENDIAN;
}

static int color_is(
    const uint8_t* rgba,
    uint32_t width,
    uint32_t x,
    uint32_t y,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a
) {
    const uint8_t* pixel = rgba + ((y * width + x) * 4);
    return pixel[0] == r && pixel[1] == g && pixel[2] == b && pixel[3] == a;
}

static int test_texture_formats_and_tluts(void) {
    uint8_t output[8 * 8 * 4];
    uint8_t i4_data[32];
    uint8_t ia4_data[32];
    uint8_t ia8_data[32];
    uint8_t rgb565_data[32];
    uint8_t rgb5a3_data[32];
    uint8_t rgba8_data[64];
    uint8_t c4_data[32];
    uint8_t c4_tlut[16 * 2];
    uint8_t c8_data[32];
    uint8_t c8_tlut[256 * 2];
    uint8_t c14_data[32];
    uint8_t c14_tlut[ACGC_RENDERER_FIXTURE_MAX_TLUT_ENTRIES * 2];
    uint8_t cmpr_data[32];
    AcgcRendererFixtureTextureDescription description;
    uint32_t i;

    CHECK(acgc_renderer_fixture_texture_bytes(8, 8,
              ACGC_RENDERER_FIXTURE_TF_I4) == 32);
    CHECK(acgc_renderer_fixture_texture_bytes(4, 4,
              ACGC_RENDERER_FIXTURE_TF_RGBA8) == 64);
    CHECK(acgc_renderer_fixture_texture_bytes(4, 4,
              ACGC_RENDERER_FIXTURE_TF_C14X2) == 32);
    CHECK(acgc_renderer_fixture_texture_bytes(0, 4,
              ACGC_RENDERER_FIXTURE_TF_I4) == 0);

    memset(i4_data, 0, sizeof(i4_data));
    i4_data[0] = 0xF0;
    i4_data[31] = 0x1E;
    set_texture_description(&description, 8, 8,
                            ACGC_RENDERER_FIXTURE_TF_I4, sizeof(i4_data));
    CHECK(acgc_renderer_fixture_decode_texture(
              &description, i4_data, NULL, output, sizeof(output)));
    CHECK(color_is(output, 8, 0, 0, 255, 255, 255, 255));
    CHECK(color_is(output, 8, 1, 0, 0, 0, 0, 0));
    CHECK(color_is(output, 8, 7, 7, 238, 238, 238, 238));
    CHECK(!acgc_renderer_fixture_decode_texture(
              &description, i4_data, NULL, output, 8));
    description.data_size = sizeof(i4_data) - 1;
    CHECK(!acgc_renderer_fixture_decode_texture(
              &description, i4_data, NULL, output, sizeof(output)));

    memset(ia4_data, 0, sizeof(ia4_data));
    ia4_data[0] = 0xF1;
    set_texture_description(&description, 8, 4,
                            ACGC_RENDERER_FIXTURE_TF_IA4, sizeof(ia4_data));
    CHECK(acgc_renderer_fixture_decode_texture(
              &description, ia4_data, NULL, output, sizeof(output)));
    CHECK(color_is(output, 8, 0, 0, 0x11, 0x11, 0x11, 0xFF));

    for (i = 0; i < sizeof(ia8_data); i += 2) {
        ia8_data[i] = (uint8_t)(0x10 + i);
        ia8_data[i + 1] = (uint8_t)(0x80 + i);
    }
    set_texture_description(&description, 4, 4,
                            ACGC_RENDERER_FIXTURE_TF_IA8, sizeof(ia8_data));
    CHECK(acgc_renderer_fixture_decode_texture(
              &description, ia8_data, NULL, output, sizeof(output)));
    CHECK(color_is(output, 4, 0, 0, 0x80, 0x80, 0x80, 0x10));
    CHECK(color_is(output, 4, 3, 3, 0x9E, 0x9E, 0x9E, 0x2E));

    memset(rgb565_data, 0, sizeof(rgb565_data));
    store_u16(rgb565_data, 0xF800, ACGC_RENDERER_FIXTURE_BIG_ENDIAN);
    set_texture_description(&description, 4, 4,
                            ACGC_RENDERER_FIXTURE_TF_RGB565, sizeof(rgb565_data));
    CHECK(acgc_renderer_fixture_decode_texture(
              &description, rgb565_data, NULL, output, sizeof(output)));
    CHECK(color_is(output, 4, 0, 0, 255, 0, 0, 255));

    memset(rgb5a3_data, 0, sizeof(rgb5a3_data));
    store_u16(rgb5a3_data, 0x83E0, ACGC_RENDERER_FIXTURE_BIG_ENDIAN);
    set_texture_description(&description, 4, 4,
                            ACGC_RENDERER_FIXTURE_TF_RGB5A3, sizeof(rgb5a3_data));
    CHECK(acgc_renderer_fixture_decode_texture(
              &description, rgb5a3_data, NULL, output, sizeof(output)));
    CHECK(color_is(output, 4, 0, 0, 0, 255, 0, 255));

    memset(rgba8_data, 0, sizeof(rgba8_data));
    rgba8_data[0] = 0x10;
    rgba8_data[1] = 0x20;
    rgba8_data[32] = 0x30;
    rgba8_data[33] = 0x40;
    rgba8_data[30] = 0x50;
    rgba8_data[31] = 0x60;
    rgba8_data[62] = 0x70;
    rgba8_data[63] = 0x80;
    set_texture_description(&description, 4, 4,
                            ACGC_RENDERER_FIXTURE_TF_RGBA8, sizeof(rgba8_data));
    CHECK(acgc_renderer_fixture_decode_texture(
              &description, rgba8_data, NULL, output, sizeof(output)));
    CHECK(color_is(output, 4, 0, 0, 0x20, 0x30, 0x40, 0x10));
    CHECK(color_is(output, 4, 3, 3, 0x60, 0x70, 0x80, 0x50));

    memset(c4_data, 0, sizeof(c4_data));
    c4_data[0] = 0x01;
    store_u16(c4_tlut + 0, 0x0000, ACGC_RENDERER_FIXTURE_BIG_ENDIAN);
    store_u16(c4_tlut + 2, 0x83E0, ACGC_RENDERER_FIXTURE_BIG_ENDIAN);
    set_texture_description(&description, 8, 8,
                            ACGC_RENDERER_FIXTURE_TF_C4, sizeof(c4_data));
    description.tlut_format = ACGC_RENDERER_FIXTURE_TL_RGB5A3;
    description.tlut_entries = 16;
    description.tlut_data_size = sizeof(c4_tlut);
    CHECK(acgc_renderer_fixture_decode_texture(
              &description, c4_data, c4_tlut, output, sizeof(output)));
    CHECK(color_is(output, 8, 0, 0, 0, 0, 0, 0));
    CHECK(color_is(output, 8, 1, 0, 0, 255, 0, 255));

    memset(c8_data, 0x2A, sizeof(c8_data));
    memset(c8_tlut, 0, sizeof(c8_tlut));
    store_u16(c8_tlut + 0x2A * 2, 0x7F20, ACGC_RENDERER_FIXTURE_BIG_ENDIAN);
    set_texture_description(&description, 8, 4,
                            ACGC_RENDERER_FIXTURE_TF_C8, sizeof(c8_data));
    description.tlut_format = ACGC_RENDERER_FIXTURE_TL_IA8;
    description.tlut_entries = 256;
    description.tlut_data_size = sizeof(c8_tlut);
    CHECK(acgc_renderer_fixture_decode_texture(
              &description, c8_data, c8_tlut, output, sizeof(output)));
    CHECK(color_is(output, 8, 0, 0, 0x7F, 0x7F, 0x7F, 0x20));
    description.tlut_byte_order = ACGC_RENDERER_FIXTURE_LITTLE_ENDIAN;
    store_u16(c8_tlut + 0x2A * 2, 0x207F, ACGC_RENDERER_FIXTURE_LITTLE_ENDIAN);
    CHECK(acgc_renderer_fixture_decode_texture(
              &description, c8_data, c8_tlut, output, sizeof(output)));
    CHECK(color_is(output, 8, 0, 0, 0x7F, 0x7F, 0x7F, 0x20));

    memset(c14_data, 0, sizeof(c14_data));
    for (i = 0; i < 16; i++) {
        store_u16(c14_data + i * 2, (uint16_t)(0x4001 + (i & 1)),
                  ACGC_RENDERER_FIXTURE_BIG_ENDIAN);
    }
    memset(c14_tlut, 0, sizeof(c14_tlut));
    store_u16(c14_tlut + 1 * 2, 0xF800, ACGC_RENDERER_FIXTURE_BIG_ENDIAN);
    store_u16(c14_tlut + 2 * 2, 0x001F, ACGC_RENDERER_FIXTURE_BIG_ENDIAN);
    set_texture_description(&description, 4, 4,
                            ACGC_RENDERER_FIXTURE_TF_C14X2, sizeof(c14_data));
    description.tlut_format = ACGC_RENDERER_FIXTURE_TL_RGB565;
    description.tlut_entries = ACGC_RENDERER_FIXTURE_MAX_TLUT_ENTRIES;
    description.tlut_data_size = sizeof(c14_tlut);
    CHECK(acgc_renderer_fixture_decode_texture(
              &description, c14_data, c14_tlut, output, sizeof(output)));
    CHECK(color_is(output, 4, 0, 0, 255, 0, 0, 255));
    CHECK(color_is(output, 4, 1, 0, 0, 0, 255, 255));

    memset(cmpr_data, 0, sizeof(cmpr_data));
    store_u16(cmpr_data + 0, 0xF800, ACGC_RENDERER_FIXTURE_BIG_ENDIAN);
    store_u16(cmpr_data + 2, 0x001F, ACGC_RENDERER_FIXTURE_BIG_ENDIAN);
    set_texture_description(&description, 8, 8,
                            ACGC_RENDERER_FIXTURE_TF_CMPR, sizeof(cmpr_data));
    CHECK(acgc_renderer_fixture_decode_texture(
              &description, cmpr_data, NULL, output, sizeof(output)));
    CHECK(color_is(output, 8, 0, 0, 255, 0, 0, 255));

    return 0;
}

static int test_sampler_resolution(void) {
    AcgcRendererFixtureSamplerDescription description = {
        ACGC_RENDERER_FIXTURE_VERSION,
        ACGC_RENDERER_FIXTURE_WRAP_REPEAT,
        ACGC_RENDERER_FIXTURE_WRAP_MIRROR,
        ACGC_RENDERER_FIXTURE_FILTER_LINEAR,
        ACGC_RENDERER_FIXTURE_FILTER_NEAREST,
        1
    };
    AcgcRendererFixtureSamplerState state;

    CHECK(acgc_renderer_fixture_resolve_sampler(&description, &state));
    CHECK(state.address_s == ACGC_RENDERER_FIXTURE_WRAP_REPEAT);
    CHECK(state.address_t == ACGC_RENDERER_FIXTURE_WRAP_MIRROR);
    CHECK(state.min_filter == ACGC_RENDERER_FIXTURE_FILTER_LINEAR);
    CHECK(state.mag_filter == ACGC_RENDERER_FIXTURE_FILTER_NEAREST);

    description.filtering_enabled = 0;
    description.min_filter = ACGC_RENDERER_FIXTURE_FILTER_LINEAR_MIP_LINEAR;
    description.mag_filter = ACGC_RENDERER_FIXTURE_FILTER_NEAR_MIP_NEAR;
    CHECK(acgc_renderer_fixture_resolve_sampler(&description, &state));
    CHECK(state.min_filter == ACGC_RENDERER_FIXTURE_FILTER_NEAREST);
    CHECK(state.mag_filter == ACGC_RENDERER_FIXTURE_FILTER_NEAREST);

    description.filtering_enabled = 1;
    CHECK(!acgc_renderer_fixture_resolve_sampler(&description, &state));
    description.min_filter = ACGC_RENDERER_FIXTURE_FILTER_LINEAR;
    description.wrap_s = 3;
    CHECK(!acgc_renderer_fixture_resolve_sampler(&description, &state));
    description.wrap_s = ACGC_RENDERER_FIXTURE_WRAP_CLAMP;
    description.filtering_enabled = 0;
    description.min_filter = UINT32_C(99);
    CHECK(!acgc_renderer_fixture_resolve_sampler(&description, &state));
    return 0;
}

static void init_tev_state(AcgcRendererFixtureTevState* state) {
    memset(state, 0, sizeof(*state));
    state->version = ACGC_RENDERER_FIXTURE_VERSION;
    state->raster = (AcgcRendererFixtureColor){ 128, 64, 32, 128 };
    state->texture[0] = (AcgcRendererFixtureColor){ 128, 64, 255, 128 };
    state->texture[1] = (AcgcRendererFixtureColor){ 64, 128, 192, 255 };
    state->konst[0] = (AcgcRendererFixtureColor){ 12, 34, 56, 78 };
}

static void init_replace_stage(AcgcRendererFixtureTevStage* stage) {
    memset(stage, 0, sizeof(*stage));
    stage->color_d = ACGC_RENDERER_FIXTURE_CC_TEXC;
    stage->alpha_d = ACGC_RENDERER_FIXTURE_CA_TEXA;
    stage->color_op = ACGC_RENDERER_FIXTURE_TEV_ADD;
    stage->color_clamp = 1;
    stage->alpha_op = ACGC_RENDERER_FIXTURE_TEV_ADD;
    stage->alpha_clamp = 1;
}

static int test_tev_combiners(void) {
    AcgcRendererFixtureTevState state;
    AcgcRendererFixtureColor output;

    init_tev_state(&state);
    state.stage_count = 1;
    init_replace_stage(&state.stages[0]);
    CHECK(acgc_renderer_fixture_tev_evaluate(&state, &output));
    CHECK(output.r == 128 && output.g == 64 && output.b == 255 && output.a == 128);

    state.stages[0].color_a = ACGC_RENDERER_FIXTURE_CC_ZERO;
    state.stages[0].color_b = ACGC_RENDERER_FIXTURE_CC_TEXC;
    state.stages[0].color_c = ACGC_RENDERER_FIXTURE_CC_RASC;
    state.stages[0].color_d = ACGC_RENDERER_FIXTURE_CC_ZERO;
    state.stages[0].alpha_a = ACGC_RENDERER_FIXTURE_CA_ZERO;
    state.stages[0].alpha_b = ACGC_RENDERER_FIXTURE_CA_TEXA;
    state.stages[0].alpha_c = ACGC_RENDERER_FIXTURE_CA_RASA;
    state.stages[0].alpha_d = ACGC_RENDERER_FIXTURE_CA_ZERO;
    CHECK(acgc_renderer_fixture_tev_evaluate(&state, &output));
    CHECK(output.r == 65 && output.g == 16 && output.b == 32 && output.a == 65);

    init_tev_state(&state);
    state.stage_count = 2;
    init_replace_stage(&state.stages[0]);
    state.stages[0].color_out = ACGC_RENDERER_FIXTURE_TEV_REG0;
    state.stages[0].alpha_out = ACGC_RENDERER_FIXTURE_TEV_REG0;
    memset(&state.stages[1], 0, sizeof(state.stages[1]));
    state.stages[1].color_d = ACGC_RENDERER_FIXTURE_CC_C0;
    state.stages[1].alpha_d = ACGC_RENDERER_FIXTURE_CA_A0;
    state.stages[1].color_clamp = 1;
    state.stages[1].alpha_clamp = 1;
    CHECK(acgc_renderer_fixture_tev_evaluate(&state, &output));
    CHECK(output.r == 128 && output.g == 64 && output.b == 255 && output.a == 128);

    init_tev_state(&state);
    state.stage_count = 1;
    memset(&state.stages[0], 0, sizeof(state.stages[0]));
    state.stages[0].color_d = ACGC_RENDERER_FIXTURE_CC_KONST;
    state.stages[0].alpha_d = ACGC_RENDERER_FIXTURE_CA_KONST;
    state.stages[0].konst_color_sel = 12;
    state.stages[0].konst_alpha_sel = 12;
    state.stages[0].color_clamp = 1;
    state.stages[0].alpha_clamp = 1;
    CHECK(acgc_renderer_fixture_tev_evaluate(&state, &output));
    CHECK(output.r == 12 && output.g == 34 && output.b == 56 && output.a == 0);

    init_tev_state(&state);
    state.stage_count = 1;
    memset(&state.stages[0], 0, sizeof(state.stages[0]));
    state.stages[0].color_d = ACGC_RENDERER_FIXTURE_CC_ONE;
    state.stages[0].color_bias = ACGC_RENDERER_FIXTURE_TEV_BIAS_SUB_HALF;
    state.stages[0].color_scale = ACGC_RENDERER_FIXTURE_TEV_SCALE_HALF;
    state.stages[0].color_clamp = 1;
    CHECK(acgc_renderer_fixture_tev_evaluate(&state, &output));
    CHECK(output.r == 64 && output.g == 64 && output.b == 64 && output.a == 0);

    state.stages[0].color_d = ACGC_RENDERER_FIXTURE_CC_TEXC;
    state.stages[0].color_op = ACGC_RENDERER_FIXTURE_TEV_SUB;
    state.stages[0].color_a = ACGC_RENDERER_FIXTURE_CC_ZERO;
    state.stages[0].color_b = ACGC_RENDERER_FIXTURE_CC_TEXC;
    state.stages[0].color_c = ACGC_RENDERER_FIXTURE_CC_ONE;
    state.stages[0].alpha_d = ACGC_RENDERER_FIXTURE_CA_ZERO;
    state.stages[0].alpha_op = ACGC_RENDERER_FIXTURE_TEV_SUB;
    state.stages[0].alpha_a = ACGC_RENDERER_FIXTURE_CA_ZERO;
    state.stages[0].alpha_b = ACGC_RENDERER_FIXTURE_CA_TEXA;
    state.stages[0].alpha_c = ACGC_RENDERER_FIXTURE_CA_APREV;
    CHECK(acgc_renderer_fixture_tev_evaluate(&state, &output));
    CHECK(output.r == 0 && output.g == 0 && output.b == 0 && output.a == 0);

    state.stages[0].color_op = 8;
    CHECK(!acgc_renderer_fixture_tev_evaluate(&state, &output));
    return 0;
}

int main(void) {
    CHECK(test_texture_formats_and_tluts() == 0);
    CHECK(test_sampler_resolution() == 0);
    CHECK(test_tev_combiners() == 0);
    puts("renderer fixture tests: PASS (synthetic fixed-width texture/TLUT, CI14x2, sampler, and TEV coverage; no Metal or game rendering)");
    return 0;
}
