#include "pc_gx_internal.h"

#include <acgc/gx_canonical_lighting_state.h>
#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXLighting.h>
#include <dolphin/gx/GXStruct.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int g_pc_window_w = PC_SCREEN_WIDTH;
int g_pc_window_h = PC_SCREEN_HEIGHT;
int g_pc_widescreen_stretch = 0;

void pc_gx_tev_seq_reset(void) {
}

static PCGXShaderVariant g_fixture_shader_variant;

PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    return &g_fixture_shader_variant;
}

static void fixture_gl_bind_vertex_array(GLuint array) {
    (void)array;
}

static void fixture_gl_bind_buffer(GLenum target, GLuint buffer) {
    (void)target;
    (void)buffer;
}

void GXLoadLightObjIndx(u32 object_index, u32 light);
void GXInitSpecularDir(void* light_object, f32 nx, f32 ny, f32 nz);
void GXInitSpecularDirHA(
    void* light_object,
    f32 nx,
    f32 ny,
    f32 nz,
    f32 hx,
    f32 hy,
    f32 hz
);

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct {
    int calls;
    PCGXRawLighting before;
} LightingFlushObservation;

static uint32_t float_bits(f32 value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static GXColor fixture_color(
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
) {
    GXColor color = { red, green, blue, alpha };
    return color;
}

static uint32_t logical_color(uint32_t color) {
    const uint8_t* bytes = (const uint8_t*)&color;

    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static const PCGXRawLighting* raw_lighting(void) {
    return pc_gx_raw_lighting_shadow_fixture();
}

static void reset_state(void) {
    pc_gx_clear_geometry_flush_fixture_observer();
    memset(&g_gx, 0, sizeof(g_gx));
    pc_gx_raw_lighting_initialize();
    glad_glBindVertexArray = fixture_gl_bind_vertex_array;
    glad_glBindBuffer = fixture_gl_bind_buffer;
}

static void set_channels_known_empty(void) {
    GXSetNumChans(0);
}

static void fill_complete_object(
    GXLightObj* object,
    int slot,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
) {
    memset(object, 0, sizeof(*object));
    GXInitLightAttn(
        object,
        1.0f + (f32)slot,
        -2.0f - (f32)slot,
        3.0f + (f32)slot,
        4.0f + (f32)slot,
        -5.0f - (f32)slot,
        6.0f + (f32)slot
    );
    GXInitLightPos(
        object,
        10.0f + (f32)slot,
        -20.0f - (f32)slot,
        30.0f + (f32)slot
    );
    GXInitLightDir(
        object,
        0.25f + (f32)slot,
        -0.5f,
        0.75f
    );
    GXInitLightColor(object, fixture_color(red, green, blue, alpha));
}

static int record_matches_object(
    const AcgcGxCanonicalLightingRecord* record,
    const GXLightObj* object
) {
    uint32_t words[16];
    uint32_t index;

    memcpy(words, object, sizeof(words));
    if (record->reserved[0] != 0 || record->reserved[1] != 0 ||
        record->reserved[2] != 0 ||
        record->color_rgba8 != logical_color(words[3])) {
        return 0;
    }
    for (index = 0; index < 3; index++) {
        if (record->angular_attenuation[index] != words[4 + index] ||
            record->distance_attenuation[index] != words[7 + index] ||
            record->position[index] != words[10 + index] ||
            record->direction[index] != words[13 + index]) {
            return 0;
        }
    }
    return 1;
}

static void prepare_completed_batch(LightingFlushObservation* observation) {
    memset(observation, 0, sizeof(*observation));
    g_gx.dirty = 0;
    g_gx.in_begin = 1;
    g_gx.expected_vertex_count = 1;
    g_gx.current_vertex_idx = 1;
    g_gx.pending_verts = 0;
    g_gx.vertex_pending = 0;
    g_gx.current_primitive = GX_TRIANGLES;
    g_gx.pending_prim = GX_TRIANGLES;
}

static void observe_lighting_flush(void* context) {
    LightingFlushObservation* observation =
        (LightingFlushObservation*)context;

    observation->calls++;
    observation->before = *raw_lighting();
}

static int test_reset_empty(void) {
    AcgcGxCanonicalLightingState state;
    uint32_t slot;

    reset_state();
    CHECK(raw_lighting()->known == 1);
    CHECK(raw_lighting()->invalid == 0);
    CHECK(raw_lighting()->loaded_mask == 0);
    CHECK(raw_lighting()->unresolved_indexed_mask == 0);
    CHECK(raw_lighting()->next_generation == 0);
    for (slot = 0; slot < PC_GX_RAW_LIGHTING_SLOT_COUNT; slot++) {
        CHECK(raw_lighting()->slots[slot].known_mask == 0);
        CHECK(raw_lighting()->slots[slot].invalid_mask == 0);
        CHECK(raw_lighting()->slots[slot].generation == 0);
        CHECK(memcmp(&raw_lighting()->slots[slot].value,
                     &(AcgcGxCanonicalLightingRecord){0},
                     sizeof(raw_lighting()->slots[slot].value)) == 0);
    }
    CHECK(pc_gx_raw_lighting_build_canonical(&state));
    CHECK(state.loaded_mask == 0);
    CHECK(acgc_gx_canonical_lighting_state_validate(&state));
    return 0;
}

static int test_all_slots_and_color_conversion(void) {
    AcgcGxCanonicalLightingState state;
    GXLightObj object;
    uint32_t slot;

    reset_state();
    set_channels_known_empty();
    for (slot = 0; slot < PC_GX_RAW_LIGHTING_SLOT_COUNT; slot++) {
        fill_complete_object(&object, (int)slot,
                             (uint8_t)(0x10 + slot),
                             (uint8_t)(0x20 + slot),
                             (uint8_t)(0x30 + slot),
                             (uint8_t)(0x40 + slot));
        GXLoadLightObjImm(&object, (u32)(UINT32_C(1) << slot));
        CHECK(raw_lighting()->slots[slot].known_mask ==
              PC_GX_RAW_LIGHTING_KNOWN_ALL);
        CHECK(raw_lighting()->slots[slot].invalid_mask == 0);
        CHECK(raw_lighting()->slots[slot].generation != 0);
    }
    CHECK(raw_lighting()->loaded_mask == UINT8_C(0xFF));
    CHECK(raw_lighting()->unresolved_indexed_mask == 0);
    CHECK(pc_gx_raw_lighting_build_canonical(&state));
    CHECK(state.loaded_mask == UINT32_C(0xFF));
    CHECK(state.records[0].color_rgba8 == UINT32_C(0x40302010));
    CHECK(record_matches_object(&state.records[7], &object));
    return 0;
}

static int test_signed_zero_and_direct_attenuation(void) {
    AcgcGxCanonicalLightingState state;
    GXLightObj object;
    uint32_t words[16];

    reset_state();
    set_channels_known_empty();
    memset(&object, 0, sizeof(object));
    GXInitLightAttn(
        &object,
        -0.0f, 1.0f, -2.0f,
        3.0f, -0.0f, 5.0f
    );
    GXInitLightPos(&object, -0.0f, 7.0f, -8.0f);
    GXInitLightDir(&object, -0.0f, 9.0f, -10.0f);
    GXInitLightColor(&object, fixture_color(0x01, 0x23, 0x45, 0x67));
    memcpy(words, &object, sizeof(words));
    GXLoadLightObjImm(&object, GX_LIGHT0);
    CHECK(pc_gx_raw_lighting_build_canonical(&state));
    CHECK(state.records[0].color_rgba8 == UINT32_C(0x67452301));
    CHECK(state.records[0].angular_attenuation[0] == words[4]);
    CHECK(state.records[0].distance_attenuation[1] == words[8]);
    CHECK(state.records[0].position[0] == words[10]);
    CHECK(state.records[0].direction[0] == words[13]);
    CHECK(state.records[0].direction[2] == words[15]);
    return 0;
}

static int test_constructor_defaults_and_specular(void) {
    AcgcGxCanonicalLightingState state;
    GXLightObj object;
    GXLightObj specular;
    uint32_t words[16];

    reset_state();
    set_channels_known_empty();
    memset(&object, 0, sizeof(object));
    GXInitLightSpot(&object, -1.0f, GX_SP_COS);
    GXInitLightDistAttn(&object, -1.0f, 0.5f, GX_DA_STEEP);
    GXInitLightPos(&object, 1.0f, 2.0f, 3.0f);
    GXInitLightDir(&object, 4.0f, 5.0f, 6.0f);
    GXInitLightColor(&object, fixture_color(1, 2, 3, 4));
    GXLoadLightObjImm(&object, GX_LIGHT0);
    memcpy(words, &object, sizeof(words));
    CHECK(words[4] == float_bits(1.0f));
    CHECK(words[5] == float_bits(0.0f));
    CHECK(words[6] == float_bits(0.0f));
    CHECK(words[7] == float_bits(1.0f));
    CHECK(words[8] == float_bits(0.0f));
    CHECK(words[9] == float_bits(0.0f));
    CHECK(pc_gx_raw_lighting_build_canonical(&state));

    memset(&specular, 0, sizeof(specular));
    GXInitSpecularDir(&specular, 0.25f, 0.5f, 0.75f);
    GXInitLightAttn(&specular, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    GXInitLightColor(&specular, fixture_color(5, 6, 7, 8));
    GXLoadLightObjImm(&specular, GX_LIGHT1);
    CHECK(pc_gx_raw_lighting_build_canonical(&state));
    CHECK(record_matches_object(&state.records[1], &specular));

    memset(&specular, 0, sizeof(specular));
    GXInitSpecularDirHA(&specular, 1.0f, 2.0f, 3.0f,
                        0.1f, 0.2f, 0.3f);
    GXInitLightAttn(&specular, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    GXInitLightColor(&specular, fixture_color(9, 10, 11, 12));
    GXLoadLightObjImm(&specular, GX_LIGHT2);
    CHECK(pc_gx_raw_lighting_build_canonical(&state));
    CHECK(record_matches_object(&state.records[2], &specular));
    return 0;
}

static int test_indexed_repair_and_equality(void) {
    AcgcGxCanonicalLightingState state;
    GXLightObj object;
    uint64_t first_generation;
    uint32_t dirty_before;

    reset_state();
    set_channels_known_empty();
    fill_complete_object(&object, 0, 0x10, 0x20, 0x30, 0x40);
    GXLoadLightObjImm(&object, GX_LIGHT0);
    first_generation = raw_lighting()->slots[0].generation;
    dirty_before = g_gx.dirty;
    g_gx.dirty = 0;
    GXLoadLightObjImm(&object, GX_LIGHT0);
    CHECK(raw_lighting()->slots[0].generation > first_generation);
    CHECK(g_gx.dirty == 0);
    CHECK(dirty_before != 0);

    GXLoadLightObjIndx(UINT32_C(0x1234), GX_LIGHT0);
    CHECK((raw_lighting()->loaded_mask & 1) != 0);
    CHECK((raw_lighting()->unresolved_indexed_mask & 1) != 0);
    CHECK(!pc_gx_raw_lighting_build_canonical(&state));
    g_gx.dirty = 0;
    GXLoadLightObjImm(&object, GX_LIGHT0);
    CHECK((raw_lighting()->unresolved_indexed_mask & 1) == 0);
    CHECK(raw_lighting()->slots[0].generation > first_generation);
    CHECK(g_gx.dirty == 0);
    CHECK(pc_gx_raw_lighting_build_canonical(&state));
    return 0;
}

static int test_channels_reference_validation(void) {
    AcgcGxCanonicalLightingState state;
    GXLightObj object;

    reset_state();
    GXSetNumChans(1);
    GXSetChanCtrl(
        GX_COLOR0A0, GX_TRUE, GX_SRC_REG, GX_SRC_REG,
        GX_LIGHT1, GX_DF_NONE, GX_AF_NONE
    );
    fill_complete_object(&object, 0, 1, 2, 3, 4);
    GXLoadLightObjImm(&object, GX_LIGHT0);
    CHECK(!pc_gx_raw_lighting_build_canonical(&state));

    GXSetChanCtrl(
        GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG,
        GX_LIGHT1, GX_DF_NONE, GX_AF_NONE
    );
    CHECK(pc_gx_raw_lighting_build_canonical(&state));

    GXSetChanCtrl(
        GX_COLOR0A0, GX_TRUE, GX_SRC_REG, GX_SRC_REG,
        GX_LIGHT0, GX_DF_NONE, GX_AF_NONE
    );
    CHECK(pc_gx_raw_lighting_build_canonical(&state));
    GXLoadLightObjIndx(UINT32_C(0x55), GX_LIGHT0);
    CHECK(!pc_gx_raw_lighting_build_canonical(&state));
    return 0;
}

static int test_invalidity_and_all_or_nothing(void) {
    AcgcGxCanonicalLightingState state;
    GXLightObj object;
    AcgcGxCanonicalLightingState sentinel;
    PCGXRawLighting before;

    reset_state();
    set_channels_known_empty();
    fill_complete_object(&object, 0, 1, 2, 3, 4);
    GXLoadLightObjImm(&object, GX_LIGHT0);
    before = *raw_lighting();
    GXLoadLightObjImm(&object, GX_LIGHT_NULL);
    CHECK(raw_lighting()->invalid != 0);
    CHECK(raw_lighting()->loaded_mask == before.loaded_mask);
    CHECK(raw_lighting()->slots[0].generation == before.slots[0].generation);
    memset(&sentinel, 0xA5, sizeof(sentinel));
    CHECK(!pc_gx_raw_lighting_build_canonical(&sentinel));
    CHECK(sentinel.loaded_mask == UINT32_C(0xA5A5A5A5));

    reset_state();
    set_channels_known_empty();
    fill_complete_object(&object, 0, 1, 2, 3, 4);
    GXLoadLightObjImm(&object, GX_LIGHT0);
    before = *raw_lighting();
    GXLoadLightObjImm(&object, 0x003);
    CHECK(raw_lighting()->invalid != 0);
    CHECK(memcmp(raw_lighting(), &before, sizeof(before)) != 0);
    CHECK(raw_lighting()->loaded_mask == before.loaded_mask);
    CHECK(raw_lighting()->slots[0].generation == before.slots[0].generation);

    reset_state();
    set_channels_known_empty();
    before = *raw_lighting();
    GXLoadLightObjImm(NULL, GX_LIGHT0);
    CHECK(raw_lighting()->invalid != 0);
    CHECK(raw_lighting()->loaded_mask == before.loaded_mask);
    CHECK(raw_lighting()->slots[0].generation == before.slots[0].generation);

    reset_state();
    set_channels_known_empty();
    fill_complete_object(&object, 0, 1, 2, 3, 4);
    GXInitLightPos(&object, NAN, 2.0f, 3.0f);
    GXLoadLightObjImm(&object, GX_LIGHT0);
    CHECK(raw_lighting()->invalid != 0);
    CHECK(raw_lighting()->loaded_mask == 0);
    CHECK(raw_lighting()->slots[0].invalid_mask != 0);
    CHECK(!pc_gx_raw_lighting_build_canonical(&state));

    reset_state();
    set_channels_known_empty();
    CHECK(raw_lighting()->known == 1);
    GXLoadLightObjIndx(UINT32_C(0x22), 0x100);
    CHECK(raw_lighting()->invalid != 0);
    CHECK(raw_lighting()->loaded_mask == 0);
    CHECK(raw_lighting()->slots[0].generation == 0);
    return 0;
}

static int test_sticky_reset_and_reserved_zeroing(void) {
    AcgcGxCanonicalLightingState state;
    GXLightObj object;
    uint32_t words[16];

    reset_state();
    set_channels_known_empty();
    fill_complete_object(&object, 0, 1, 2, 3, 4);
    memcpy(words, &object, sizeof(words));
    words[0] = UINT32_C(0xDEADBEEF);
    words[1] = UINT32_C(0x10203040);
    words[2] = UINT32_C(0x50607080);
    memcpy(&object, words, sizeof(object));
    GXLoadLightObjImm(&object, GX_LIGHT0);
    CHECK(raw_lighting()->slots[0].value.reserved[0] == 0);
    CHECK(raw_lighting()->slots[0].value.reserved[1] == 0);
    CHECK(raw_lighting()->slots[0].value.reserved[2] == 0);

    GXLoadLightObjImm(&object, 0x100);
    CHECK(raw_lighting()->invalid != 0);
    GXLoadLightObjImm(&object, GX_LIGHT1);
    CHECK((raw_lighting()->loaded_mask & 2) == 0);
    reset_state();
    CHECK(raw_lighting()->known == 1);
    CHECK(raw_lighting()->invalid == 0);
    CHECK(pc_gx_raw_lighting_build_canonical(&state));
    return 0;
}

static int test_old_batch_ordering(void) {
    GXLightObj old_object;
    GXLightObj new_object;
    LightingFlushObservation observation;

    reset_state();
    set_channels_known_empty();
    fill_complete_object(&old_object, 0, 0x10, 0x20, 0x30, 0x40);
    fill_complete_object(&new_object, 0, 0x50, 0x60, 0x70, 0x80);
    GXLoadLightObjImm(&old_object, GX_LIGHT0);
    g_gx.dirty = 0;
    prepare_completed_batch(&observation);
    pc_gx_set_geometry_flush_fixture_observer(
        observe_lighting_flush,
        &observation
    );
    GXLoadLightObjImm(&new_object, GX_LIGHT0);
    pc_gx_clear_geometry_flush_fixture_observer();
    CHECK(observation.calls == 1);
    CHECK(observation.before.slots[0].value.color_rgba8 ==
          UINT32_C(0x40302010));
    CHECK(raw_lighting()->slots[0].value.color_rgba8 ==
          UINT32_C(0x80706050));
    return 0;
}

int main(void) {
    CHECK(test_reset_empty() == 0);
    CHECK(test_all_slots_and_color_conversion() == 0);
    CHECK(test_signed_zero_and_direct_attenuation() == 0);
    CHECK(test_constructor_defaults_and_specular() == 0);
    CHECK(test_indexed_repair_and_equality() == 0);
    CHECK(test_channels_reference_validation() == 0);
    CHECK(test_invalidity_and_all_or_nothing() == 0);
    CHECK(test_sticky_reset_and_reserved_zeroing() == 0);
    CHECK(test_old_batch_ordering() == 0);
    puts("pc GX raw Lighting shadow fixture: PASS");
    puts("proof boundary: pointer-free CPU GX light register provenance, canonical validation, channel dependency, and flush/equality ordering only; no renderer, Metal, device, pixel, Windows runtime, or playability claim");
    return 0;
}
