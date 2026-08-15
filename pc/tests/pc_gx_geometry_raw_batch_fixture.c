#include "pc_gx_internal.h"

#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXStruct.h>
#include <dolphin/gx/GXGeometry.h>
#include <dolphin/gx/GXVert.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void GXSetZMode(GXBool compare_enable, u32 func, GXBool update_enable);

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

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct {
    int calls;
    uint32_t primitive;
    uint32_t vertex_count;
    uint32_t known;
    uint32_t invalid;
    uint32_t position_vcd;
    uint32_t position_vat_count;
    uint32_t position_vat_type;
    uint32_t position_value_count;
    uint32_t position_index_count;
    uint32_t position_index_stride;
    uint64_t position_array_generation;
    uint32_t position_word0;
    uint32_t position_index0;
    int in_begin;
} GeometryFlushObservation;

static const PCGXRawGeometry* geometry_shadow(void) {
    return pc_gx_raw_geometry_shadow_fixture();
}

static void observe_geometry_flush(void* context) {
    GeometryFlushObservation* observation =
        (GeometryFlushObservation*)context;
    const PCGXRawGeometryBatch* batch = &geometry_shadow()->completed;
    const PCGXRawGeometryAttribute* position = &batch->attr[GX_VA_POS];

    observation->calls++;
    observation->primitive = batch->primitive;
    observation->vertex_count = batch->vertex_count;
    observation->known = batch->known;
    observation->invalid = batch->invalid;
    observation->position_vcd = position->vcd_type;
    observation->position_vat_count = position->vat_count;
    observation->position_vat_type = position->vat_type;
    observation->position_value_count = position->value_count;
    observation->position_index_count = position->index_count;
    observation->position_index_stride = position->index_stride;
    observation->position_array_generation = position->array_generation;
    observation->position_word0 = position->value_words[0][0];
    observation->position_index0 = position->index_values[0];
    observation->in_begin = g_gx.in_begin;
}

static void reset_state(void) {
    pc_gx_clear_geometry_flush_fixture_observer();
    memset(&g_gx, 0, sizeof(g_gx));
    glad_glBindVertexArray = fixture_gl_bind_vertex_array;
    glad_glBindBuffer = fixture_gl_bind_buffer;
}

static void configure_direct_position(void) {
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
}

static void emit_direct_triangle(void) {
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXPosition3f32(7.0f, 8.0f, 9.0f);
}

static int finish_indexed_position_host_triangle(
    const f32 expected[3][3]
) {
    int vertex;
    int component;

    for (vertex = 0; vertex < 2; vertex++) {
        for (component = 0; component < 3; component++) {
            CHECK(g_gx.vertex_buffer[vertex].position[component] ==
                  expected[vertex][component]);
        }
    }
    for (component = 0; component < 3; component++) {
        CHECK(g_gx.current_vertex.position[component] ==
              expected[2][component]);
    }
    GXEnd();
    CHECK(geometry_shadow()->completed.known == 1);
    CHECK(geometry_shadow()->completed.invalid == 0);
    return 0;
}

static int test_indexed_position_host_scalar_forms(void) {
    {
        static const uint8_t values[3][3] = {
            {8, 12, 16}, {20, 24, 28}, {32, 36, 40}
        };
        static const f32 expected[3][3] = {
            {2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f},
            {8.0f, 9.0f, 10.0f}
        };
        reset_state();
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_U8, 2);
        GXSetArray(GX_VA_POS, values, sizeof(values), sizeof(values[0]));
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GXPosition1x8(0);
        GXPosition1x8(1);
        GXPosition1x8(2);
        CHECK(finish_indexed_position_host_triangle(expected) == 0);
    }
    {
        static const int8_t values[3][3] = {
            {-4, 2, 6}, {-8, 4, 10}, {-12, 6, 14}
        };
        static const f32 expected[3][3] = {
            {-2.0f, 1.0f, 3.0f}, {-4.0f, 2.0f, 5.0f},
            {-6.0f, 3.0f, 7.0f}
        };
        reset_state();
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S8, 1);
        GXSetArray(GX_VA_POS, values, sizeof(values), sizeof(values[0]));
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GXPosition1x8(0);
        GXPosition1x8(1);
        GXPosition1x8(2);
        CHECK(finish_indexed_position_host_triangle(expected) == 0);
    }
    {
        static const uint8_t values[3][2] = {{4, 8}, {12, 16}, {20, 24}};
        static const f32 expected[3][3] = {
            {2.0f, 4.0f, 0.0f}, {6.0f, 8.0f, 0.0f},
            {10.0f, 12.0f, 0.0f}
        };
        reset_state();
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_U8, 1);
        GXSetArray(GX_VA_POS, values, sizeof(values), sizeof(values[0]));
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GXPosition1x8(0);
        GXPosition1x8(1);
        GXPosition1x8(2);
        CHECK(finish_indexed_position_host_triangle(expected) == 0);
    }
    {
        static const uint16_t values[3][3] = {
            {16, 24, 32}, {40, 48, 56}, {64, 72, 80}
        };
        static const f32 expected[3][3] = {
            {2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f},
            {8.0f, 9.0f, 10.0f}
        };
        reset_state();
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_U16, 3);
        GXSetArray(GX_VA_POS, values, sizeof(values), sizeof(values[0]));
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GXPosition1x8(0);
        GXPosition1x8(1);
        GXPosition1x8(2);
        CHECK(finish_indexed_position_host_triangle(expected) == 0);
    }
    {
        static const int16_t values[3][3] = {
            {-8, 4, 12}, {-16, 8, 20}, {-24, 12, 28}
        };
        static const f32 expected[3][3] = {
            {-2.0f, 1.0f, 3.0f}, {-4.0f, 2.0f, 5.0f},
            {-6.0f, 3.0f, 7.0f}
        };
        reset_state();
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 2);
        GXSetArray(GX_VA_POS, values, sizeof(values), sizeof(values[0]));
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GXPosition1x8(0);
        GXPosition1x8(1);
        GXPosition1x8(2);
        CHECK(finish_indexed_position_host_triangle(expected) == 0);
    }
    {
        static const f32 values[3][3] = {
            {1.25f, 2.5f, 3.75f}, {4.25f, 5.5f, 6.75f},
            {7.25f, 8.5f, 9.75f}
        };
        reset_state();
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetArray(GX_VA_POS, values, sizeof(values), sizeof(values[0]));
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GXPosition1x8(0);
        GXPosition1x8(1);
        GXPosition1x8(2);
        CHECK(finish_indexed_position_host_triangle(values) == 0);
    }
    return 0;
}

static int finish_indexed_texcoord_host_triangle(
    const f32 expected[3][2]
) {
    int vertex;
    int component;

    for (vertex = 0; vertex < 2; vertex++) {
        for (component = 0; component < 2; component++) {
            CHECK(g_gx.vertex_buffer[vertex].texcoord[0][component] ==
                  expected[vertex][component]);
        }
    }
    for (component = 0; component < 2; component++) {
        CHECK(g_gx.current_vertex.texcoord[0][component] ==
              expected[2][component]);
    }
    GXEnd();
    CHECK(geometry_shadow()->completed.known == 1);
    CHECK(geometry_shadow()->completed.invalid == 0);
    return 0;
}

static void begin_indexed_texcoord_triangle(
    const void* values,
    uint32_t size,
    uint8_t stride,
    uint32_t type,
    uint8_t fraction
) {
    configure_direct_position();
    GXSetVtxDesc(GX_VA_TEX0, GX_INDEX8);
    GXSetVtxAttrFmt(
        GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, type, fraction);
    GXSetArray(GX_VA_TEX0, values, size, stride);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXTexCoord1x8(0);
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXTexCoord1x8(1);
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXTexCoord1x8(2);
}

static int test_indexed_texcoord_host_scalar_forms(void) {
    {
        static const uint8_t values[3][2] = {{4, 8}, {12, 16}, {20, 24}};
        static const f32 expected[3][2] = {{1, 2}, {3, 4}, {5, 6}};
        reset_state();
        begin_indexed_texcoord_triangle(
            values, sizeof(values), sizeof(values[0]), GX_U8, 2);
        CHECK(finish_indexed_texcoord_host_triangle(expected) == 0);
    }
    {
        static const int8_t values[3][2] = {{-4, 2}, {-8, 4}, {-12, 6}};
        static const f32 expected[3][2] = {{-2, 1}, {-4, 2}, {-6, 3}};
        reset_state();
        begin_indexed_texcoord_triangle(
            values, sizeof(values), sizeof(values[0]), GX_S8, 1);
        CHECK(finish_indexed_texcoord_host_triangle(expected) == 0);
    }
    {
        static const uint8_t values[3] = {2, 4, 6};
        static const f32 expected[3][2] = {{1, 0}, {2, 0}, {3, 0}};
        reset_state();
        configure_direct_position();
        GXSetVtxDesc(GX_VA_TEX0, GX_INDEX8);
        GXSetVtxAttrFmt(
            GX_VTXFMT0, GX_VA_TEX0, GX_TEX_S, GX_U8, 1);
        GXSetArray(GX_VA_TEX0, values, sizeof(values), sizeof(values[0]));
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GXPosition3f32(1.0f, 2.0f, 3.0f);
        GXTexCoord1x8(0);
        GXPosition3f32(4.0f, 5.0f, 6.0f);
        GXTexCoord1x8(1);
        GXPosition3f32(7.0f, 8.0f, 9.0f);
        GXTexCoord1x8(2);
        CHECK(finish_indexed_texcoord_host_triangle(expected) == 0);
    }
    {
        static const uint16_t values[3][2] = {{8, 16}, {24, 32}, {40, 48}};
        static const f32 expected[3][2] = {{1, 2}, {3, 4}, {5, 6}};
        reset_state();
        begin_indexed_texcoord_triangle(
            values, sizeof(values), sizeof(values[0]), GX_U16, 3);
        CHECK(finish_indexed_texcoord_host_triangle(expected) == 0);
    }
    {
        static const int16_t values[3][2] = {{-8, 4}, {-16, 8}, {-24, 12}};
        static const f32 expected[3][2] = {{-2, 1}, {-4, 2}, {-6, 3}};
        reset_state();
        begin_indexed_texcoord_triangle(
            values, sizeof(values), sizeof(values[0]), GX_S16, 2);
        CHECK(finish_indexed_texcoord_host_triangle(expected) == 0);
    }
    {
        static const f32 values[3][2] = {
            {1.25f, 2.5f}, {4.25f, 5.5f}, {7.25f, 8.5f}
        };
        reset_state();
        begin_indexed_texcoord_triangle(
            values, sizeof(values), sizeof(values[0]), GX_F32, 0);
        CHECK(finish_indexed_texcoord_host_triangle(values) == 0);
    }
    return 0;
}

static int finish_indexed_normal_host_triangle(
    const f32 expected[3][3]
) {
    int vertex;
    int component;

    for (vertex = 0; vertex < 2; vertex++) {
        for (component = 0; component < 3; component++) {
            CHECK(fabsf(g_gx.vertex_buffer[vertex].normal[component] -
                        expected[vertex][component]) < 0.0001f);
        }
    }
    for (component = 0; component < 3; component++) {
        CHECK(fabsf(g_gx.current_vertex.normal[component] -
                    expected[2][component]) < 0.0001f);
    }
    GXEnd();
    CHECK(geometry_shadow()->completed.known == 1);
    CHECK(geometry_shadow()->completed.invalid == 0);
    return 0;
}

static void begin_indexed_normal_triangle(
    const void* values,
    uint32_t size,
    uint8_t stride,
    uint32_t type
) {
    configure_direct_position();
    GXSetVtxDesc(GX_VA_NRM, GX_INDEX8);
    GXSetVtxAttrFmt(
        GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, type, 0);
    GXSetArray(GX_VA_NRM, values, size, stride);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXNormal1x8(0);
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXNormal1x8(1);
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXNormal1x8(2);
}

static int test_indexed_normal_host_scalar_forms(void) {
    {
        static const int8_t values[3][3] = {
            {127, -64, 0}, {-127, 32, 64}, {0, -32, 127}
        };
        static const f32 expected[3][3] = {
            {1.0f, -64.0f / 127.0f, 0.0f},
            {-1.0f, 32.0f / 127.0f, 64.0f / 127.0f},
            {0.0f, -32.0f / 127.0f, 1.0f}
        };
        reset_state();
        begin_indexed_normal_triangle(
            values, sizeof(values), sizeof(values[0]), GX_S8);
        CHECK(finish_indexed_normal_host_triangle(expected) == 0);
    }
    {
        static const int16_t values[3][3] = {
            {32767, -16384, 0}, {-32767, 8192, 16384},
            {0, -8192, 32767}
        };
        static const f32 expected[3][3] = {
            {1.0f, -16384.0f / 32767.0f, 0.0f},
            {-1.0f, 8192.0f / 32767.0f, 16384.0f / 32767.0f},
            {0.0f, -8192.0f / 32767.0f, 1.0f}
        };
        reset_state();
        begin_indexed_normal_triangle(
            values, sizeof(values), sizeof(values[0]), GX_S16);
        CHECK(finish_indexed_normal_host_triangle(expected) == 0);
    }
    {
        static const f32 values[3][3] = {
            {0.25f, -0.5f, 0.75f}, {1.25f, -1.5f, 1.75f},
            {2.25f, -2.5f, 2.75f}
        };
        reset_state();
        begin_indexed_normal_triangle(
            values, sizeof(values), sizeof(values[0]), GX_F32);
        CHECK(finish_indexed_normal_host_triangle(values) == 0);
    }
    return 0;
}

static int test_indexed_host_fallback_policy(void) {
    static const f32 position[1][3] = {{10.0f, 20.0f, 30.0f}};
    static const int8_t normal[1][3] = {{127, 0, 0}};
    static const f32 texcoord[1][2] = {{1.0f, 2.0f}};

    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetArray(GX_VA_POS, position, sizeof(position), sizeof(position[0]));
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition1x8(1);
    CHECK(g_gx.current_vertex.position[0] == 0.0f);
    CHECK(g_gx.current_vertex.position[1] == 0.0f);
    CHECK(g_gx.current_vertex.position[2] == 0.0f);
    GXEnd();
    CHECK(geometry_shadow()->completed.known == 0);
    CHECK(geometry_shadow()->completed.invalid != 0);

    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX16);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetArray(GX_VA_POS, position, sizeof(position), sizeof(position[0]));
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition1x8(0);
    CHECK(g_gx.current_vertex.position[0] == 0.0f);
    CHECK(g_gx.current_vertex.position[1] == 0.0f);
    CHECK(g_gx.current_vertex.position[2] == 0.0f);
    GXEnd();
    CHECK(geometry_shadow()->completed.known == 0);
    CHECK(geometry_shadow()->completed.invalid != 0);

    reset_state();
    configure_direct_position();
    GXSetVtxDesc(GX_VA_NRM, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_S8, 0);
    GXSetArray(GX_VA_NRM, normal, sizeof(normal), sizeof(normal[0]));
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    g_gx.current_vertex.normal[0] = 9.0f;
    g_gx.current_vertex.normal[1] = 8.0f;
    g_gx.current_vertex.normal[2] = 7.0f;
    GXNormal1x8(1);
    CHECK(g_gx.current_vertex.normal[0] == 9.0f);
    CHECK(g_gx.current_vertex.normal[1] == 8.0f);
    CHECK(g_gx.current_vertex.normal[2] == 7.0f);
    GXEnd();
    CHECK(geometry_shadow()->completed.known == 0);
    CHECK(geometry_shadow()->completed.invalid != 0);

    reset_state();
    configure_direct_position();
    GXSetVtxDesc(GX_VA_TEX0, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetArray(GX_VA_TEX0, texcoord, sizeof(texcoord), sizeof(texcoord[0]));
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    g_gx.current_vertex.texcoord[0][0] = 9.0f;
    g_gx.current_vertex.texcoord[0][1] = 8.0f;
    GXTexCoord1x8(1);
    CHECK(g_gx.current_vertex.texcoord[0][0] == 9.0f);
    CHECK(g_gx.current_vertex.texcoord[0][1] == 8.0f);
    GXEnd();
    CHECK(geometry_shadow()->completed.known == 0);
    CHECK(geometry_shadow()->completed.invalid != 0);
    return 0;
}

typedef void (*FixtureColorEmitter)(void);

static void emit_color_rgb565(void) { GXColor1u16(UINT16_C(0x1234)); }
static void emit_color_rgb8(void) { GXColor3u8(0x12, 0x34, 0x56); }
static void emit_color_rgbx8(void) { GXColor4u8(0x12, 0x34, 0x56, 0xA7); }
static void emit_color_rgba4(void) { GXColor1u16(UINT16_C(0x2345)); }
static void emit_color_rgba6(void) { GXColor3u8(0x12, 0x34, 0x56); }
static void emit_color_rgba8(void) { GXColor1u32(UINT32_C(0x12345678)); }
static void emit_color_width2_mismatch(void) {
    GXColor1u16(UINT16_C(0x1234));
}
static void emit_color_width3_mismatch(void) { GXColor3u8(0x12, 0x34, 0x56); }
static void emit_color_width4_mismatch(void) {
    GXColor4u8(0x12, 0x34, 0x56, 0x78);
}
static void emit_color_u32_mismatch(void) {
    GXColor1u32(UINT32_C(0x12345678));
}

static void configure_direct_color(uint32_t count, uint32_t type) {
    configure_direct_position();
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, count, type, 0);
}

static int finish_direct_color(
    FixtureColorEmitter emit,
    uint32_t expected_word
) {
    const PCGXRawGeometryBatch* completed;

    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    emit();
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    emit();
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    emit();
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 1);
    CHECK(completed->invalid == 0);
    CHECK(completed->attr[GX_VA_CLR0].value_count == 3);
    CHECK(completed->attr[GX_VA_CLR0].value_words[0][0] == expected_word);
    return 0;
}

static int finish_invalid_direct_color(FixtureColorEmitter emit) {
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    emit();
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    emit();
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    emit();
    GXEnd();
    CHECK(geometry_shadow()->completed.known == 0);
    CHECK(geometry_shadow()->completed.invalid != 0);
    return 0;
}

static int test_direct_packed_color_forms(void) {
    reset_state();
    configure_direct_color(GX_CLR_RGB, GX_RGB565);
    CHECK(finish_direct_color(emit_color_rgb565, UINT32_C(0x00001234)) == 0);

    reset_state();
    configure_direct_color(GX_CLR_RGB, GX_RGB8);
    CHECK(finish_direct_color(emit_color_rgb8, UINT32_C(0x00123456)) == 0);

    reset_state();
    configure_direct_color(GX_CLR_RGB, GX_RGBX8);
    CHECK(finish_direct_color(emit_color_rgbx8, UINT32_C(0x12345600)) == 0);
    CHECK(g_gx.current_vertex.color0[3] == 0xA7);

    reset_state();
    configure_direct_color(GX_CLR_RGBA, GX_RGBA4);
    CHECK(finish_direct_color(emit_color_rgba4, UINT32_C(0x00002345)) == 0);

    reset_state();
    configure_direct_color(GX_CLR_RGBA, GX_RGBA6);
    CHECK(finish_direct_color(emit_color_rgba6, UINT32_C(0x00123456)) == 0);

    reset_state();
    configure_direct_color(GX_CLR_RGBA, GX_RGBA8);
    CHECK(finish_direct_color(emit_color_rgba8, UINT32_C(0x12345678)) == 0);

    reset_state();
    configure_direct_color(GX_CLR_RGB, GX_RGB8);
    CHECK(finish_invalid_direct_color(emit_color_width2_mismatch) == 0);

    reset_state();
    configure_direct_color(GX_CLR_RGB, GX_RGBX8);
    CHECK(finish_invalid_direct_color(emit_color_width3_mismatch) == 0);

    reset_state();
    configure_direct_color(GX_CLR_RGBA, GX_RGBA4);
    CHECK(finish_invalid_direct_color(emit_color_width4_mismatch) == 0);

    reset_state();
    configure_direct_color(GX_CLR_RGB, GX_RGB565);
    CHECK(finish_invalid_direct_color(emit_color_u32_mismatch) == 0);
    return 0;
}

static int test_indexed_rgbx8_ignored_x(void) {
    static const uint8_t colors[3][4] = {
        {0x12, 0x34, 0x56, 0xA7},
        {0x23, 0x45, 0x67, 0xB8},
        {0x34, 0x56, 0x78, 0xC9}
    };
    const PCGXRawGeometryBatch* completed;

    reset_state();
    configure_direct_position();
    GXSetVtxDesc(GX_VA_CLR0, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGB, GX_RGBX8, 0);
    GXSetArray(GX_VA_CLR0, colors, sizeof(colors), sizeof(colors[0]));
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXColor1x8(0);
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXColor1x8(1);
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXColor1x8(2);
    CHECK(g_gx.current_vertex.color0[3] == colors[2][3]);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 1);
    CHECK(completed->invalid == 0);
    CHECK(completed->attr[GX_VA_CLR0].value_words[0][0] ==
          UINT32_C(0x12345600));
    CHECK(completed->attr[GX_VA_CLR0].value_words[1][0] ==
          UINT32_C(0x23456700));
    CHECK(completed->attr[GX_VA_CLR0].value_words[2][0] ==
          UINT32_C(0x34567800));
    return 0;
}

static int test_initial_unknownness(void) {
    const PCGXRawGeometry* shadow;

    reset_state();
    shadow = geometry_shadow();
    CHECK(shadow == &g_gx.raw_geometry);
    CHECK(shadow->invalid == 0);
    CHECK(shadow->next_array_generation == 0);
    CHECK(shadow->completed.known == 0);
    CHECK(shadow->completed.invalid == 0);
    CHECK(shadow->format[GX_VTXFMT0][GX_VA_POS].vcd_known == 0);
    CHECK(shadow->format[GX_VTXFMT0][GX_VA_POS].vat_known == 0);
    return 0;
}

static int test_temporal_order_and_immutable_direct_copy(void) {
    GeometryFlushObservation observation;
    const PCGXRawGeometryBatch* completed;
    PCGXRawDepth depth_before;
    float changed = 100.0f;
    uint32_t changed_bits;

    reset_state();
    GXSetZMode(GX_TRUE, GX_GREATER, GX_TRUE);
    depth_before = g_gx.raw_depth;
    g_gx.dirty = 0;
    configure_direct_position();
    memset(&observation, 0, sizeof(observation));
    pc_gx_set_geometry_flush_fixture_observer(
        observe_geometry_flush,
        &observation
    );

    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    emit_direct_triangle();
    /* The completed in-begin batch must cross the existing flush boundary
     * before the descriptor mutation below. */
    GXSetVtxDesc(GX_VA_POS, GX_NONE);

    completed = &geometry_shadow()->completed;
    CHECK(observation.calls == 1);
    CHECK(observation.in_begin == 0);
    CHECK(observation.primitive == 1);
    CHECK(observation.vertex_count == 3);
    CHECK(observation.known == 1);
    CHECK(observation.invalid == 0);
    CHECK(observation.position_vcd == GX_DIRECT);
    CHECK(observation.position_vat_count == GX_POS_XYZ);
    CHECK(observation.position_vat_type == GX_F32);
    CHECK(observation.position_value_count == 3);
    CHECK(observation.position_index_count == 0);
    CHECK(observation.position_index_stride == 0);
    CHECK(g_gx.raw_geometry.format[GX_VTXFMT0][GX_VA_POS].vcd_type == GX_NONE);
    CHECK(g_gx.pending_verts == 3);
    CHECK(memcmp(&g_gx.raw_depth, &depth_before, sizeof(depth_before)) == 0);

    /* No caller pointer was retained: mutating a source-side temporary does
     * not alter the immutable completed copy. */
    changed = 200.0f;
    memcpy(&changed_bits, &changed, sizeof(changed_bits));
    CHECK(completed->attr[GX_VA_POS].value_words[0][0] !=
          changed_bits);
    CHECK(completed->attr[GX_VA_POS].value_words[0][0] ==
          UINT32_C(0x3F800000));
    pc_gx_clear_geometry_flush_fixture_observer();
    return 0;
}

static int test_index8_provenance_and_bounds(void) {
    float positions[3][3] = {
        {10.0f, 11.0f, 12.0f},
        {20.0f, 21.0f, 22.0f},
        {30.0f, 31.0f, 32.0f}
    };
    const PCGXRawGeometryBatch* completed;
    uint64_t generation;

    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetArray(GX_VA_POS, positions, sizeof(positions), sizeof(positions[0]));
    generation = geometry_shadow()->array[GX_VA_POS].generation;
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition1x8(0);
    GXPosition1x8(1);
    GXPosition1x8(0);
    GXEnd();

    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 1);
    CHECK(completed->invalid == 0);
    CHECK(completed->attr[GX_VA_POS].vcd_type == GX_INDEX8);
    CHECK(completed->attr[GX_VA_POS].index_stride == 1);
    CHECK(completed->attr[GX_VA_POS].array_generation == generation);
    CHECK(completed->attr[GX_VA_POS].array_byte_size == sizeof(positions));
    CHECK(completed->attr[GX_VA_POS].array_stride == sizeof(positions[0]));
    CHECK(completed->attr[GX_VA_POS].value_count == 2);
    CHECK(completed->attr[GX_VA_POS].index_count == 3);
    CHECK(completed->attr[GX_VA_POS].source_indices[0] == 0);
    CHECK(completed->attr[GX_VA_POS].source_indices[1] == 1);
    CHECK(completed->attr[GX_VA_POS].source_indices[2] == 0);
    CHECK(completed->attr[GX_VA_POS].index_values[0] == 0);
    CHECK(completed->attr[GX_VA_POS].index_values[1] == 1);
    CHECK(completed->attr[GX_VA_POS].index_values[2] == 0);
    CHECK(completed->attr[GX_VA_POS].value_words[0][0] ==
          UINT32_C(0x41200000));
    positions[0][0] = 900.0f;
    CHECK(completed->attr[GX_VA_POS].value_words[0][0] ==
          UINT32_C(0x41200000));
    return 0;
}

static int test_index16_and_generation_change(void) {
    float positions[3][3] = {
        {-1.0f, -2.0f, -3.0f},
        {-4.0f, -5.0f, -6.0f},
        {-7.0f, -8.0f, -9.0f}
    };
    const PCGXRawGeometryBatch* completed;
    uint64_t first_generation;
    uint64_t second_generation;

    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX16);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetArray(GX_VA_POS, positions, sizeof(positions), sizeof(positions[0]));
    first_generation = geometry_shadow()->array[GX_VA_POS].generation;
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition1x16(2);
    GXPosition1x16(1);
    GXPosition1x16(2);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 1);
    CHECK(completed->attr[GX_VA_POS].vcd_type == GX_INDEX16);
    CHECK(completed->attr[GX_VA_POS].index_stride == 2);
    CHECK(completed->attr[GX_VA_POS].array_generation == first_generation);
    CHECK(completed->attr[GX_VA_POS].source_indices[0] == 2);
    CHECK(completed->attr[GX_VA_POS].source_indices[1] == 1);
    CHECK(completed->attr[GX_VA_POS].source_indices[2] == 2);

    GXSetArray(GX_VA_POS, positions, sizeof(positions), sizeof(positions[0]));
    second_generation = geometry_shadow()->array[GX_VA_POS].generation;
    CHECK(second_generation > first_generation);
    CHECK(completed->attr[GX_VA_POS].array_generation == first_generation);
    return 0;
}

static int test_indexed_rgba8_byte_order(void) {
    static const uint8_t colors[3][4] = {
        {0x11, 0x22, 0x33, 0x44},
        {0x55, 0x66, 0x77, 0x88},
        {0x99, 0xAA, 0xBB, 0xCC}
    };
    const PCGXRawGeometryBatch* completed;

    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxDesc(GX_VA_CLR0, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetArray(GX_VA_CLR0, colors, sizeof(colors), sizeof(colors[0]));
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXColor1x8(0);
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXColor1x8(1);
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXColor1x8(2);
    GXEnd();

    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 1);
    CHECK(completed->invalid == 0);
    CHECK(completed->attr[GX_VA_CLR0].vcd_type == GX_INDEX8);
    CHECK(completed->attr[GX_VA_CLR0].value_count == 3);
    CHECK(completed->attr[GX_VA_CLR0].value_words[0][0] ==
          UINT32_C(0x11223344));
    CHECK(completed->attr[GX_VA_CLR0].value_words[1][0] ==
          UINT32_C(0x55667788));
    CHECK(completed->attr[GX_VA_CLR0].value_words[2][0] ==
          UINT32_C(0x99AABBCC));
    return 0;
}

static void configure_direct_tex_s(uint32_t vat_type) {
    configure_direct_position();
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_S, vat_type, 0);
}

static int test_direct_tex_s_wrappers(void) {
    const PCGXRawGeometryBatch* completed;
    float f32_s[3] = {1.5f, 2.5f, 3.5f};
    uint32_t f32_words[3];

    reset_state();
    configure_direct_tex_s(GX_F32);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXTexCoord1f32(f32_s[0], 99.0f);
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXTexCoord1f32(f32_s[1], 98.0f);
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXTexCoord1f32(f32_s[2], 97.0f);
    CHECK(g_gx.current_vertex.texcoord[0][1] == 97.0f);
    GXEnd();
    completed = &geometry_shadow()->completed;
    memcpy(&f32_words[0], &f32_s[0], sizeof(f32_words[0]));
    memcpy(&f32_words[1], &f32_s[1], sizeof(f32_words[1]));
    memcpy(&f32_words[2], &f32_s[2], sizeof(f32_words[2]));
    CHECK(completed->known == 1);
    CHECK(completed->invalid == 0);
    CHECK(completed->attr[GX_VA_TEX0].vat_count == GX_TEX_S);
    CHECK(completed->attr[GX_VA_TEX0].value_word_count == 2);
    CHECK(completed->attr[GX_VA_TEX0].value_words[0][0] == f32_words[0]);
    CHECK(completed->attr[GX_VA_TEX0].value_words[1][0] == f32_words[1]);
    CHECK(completed->attr[GX_VA_TEX0].value_words[2][0] == f32_words[2]);
    CHECK(completed->attr[GX_VA_TEX0].value_words[0][1] == 0);
    CHECK(completed->attr[GX_VA_TEX0].value_words[1][1] == 0);
    CHECK(completed->attr[GX_VA_TEX0].value_words[2][1] == 0);

    reset_state();
    configure_direct_tex_s(GX_U16);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXTexCoord1u16(UINT16_C(0x1234), UINT16_C(0xABCD));
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXTexCoord1u16(UINT16_C(0x2345), UINT16_C(0xBCDE));
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXTexCoord1u16(UINT16_C(0x3456), UINT16_C(0xCDEF));
    CHECK(g_gx.current_vertex.texcoord[0][1] == (f32)UINT16_C(0xCDEF));
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 1);
    CHECK(completed->attr[GX_VA_TEX0].value_words[0][0] == UINT32_C(0x1234));
    CHECK(completed->attr[GX_VA_TEX0].value_words[1][0] == UINT32_C(0x2345));
    CHECK(completed->attr[GX_VA_TEX0].value_words[2][0] == UINT32_C(0x3456));
    CHECK(completed->attr[GX_VA_TEX0].value_words[0][1] == 0);
    CHECK(completed->attr[GX_VA_TEX0].value_words[1][1] == 0);
    CHECK(completed->attr[GX_VA_TEX0].value_words[2][1] == 0);

    reset_state();
    configure_direct_tex_s(GX_S16);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXTexCoord1s16((s16)-2, (s16)101);
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXTexCoord1s16((s16)-1, (s16)102);
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXTexCoord1s16((s16)0, (s16)103);
    CHECK(g_gx.current_vertex.texcoord[0][1] == 103.0f);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 1);
    CHECK(completed->attr[GX_VA_TEX0].value_words[0][0] == UINT32_C(0xFFFE));
    CHECK(completed->attr[GX_VA_TEX0].value_words[1][0] == UINT32_C(0xFFFF));
    CHECK(completed->attr[GX_VA_TEX0].value_words[2][0] == 0);
    CHECK(completed->attr[GX_VA_TEX0].value_words[0][1] == 0);
    CHECK(completed->attr[GX_VA_TEX0].value_words[1][1] == 0);
    CHECK(completed->attr[GX_VA_TEX0].value_words[2][1] == 0);

    reset_state();
    configure_direct_tex_s(GX_U8);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXTexCoord1u8(UINT8_C(7), UINT8_C(101));
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXTexCoord1u8(UINT8_C(8), UINT8_C(102));
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXTexCoord1u8(UINT8_C(9), UINT8_C(103));
    CHECK(g_gx.current_vertex.texcoord[0][1] == 103.0f);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 1);
    CHECK(completed->attr[GX_VA_TEX0].value_words[0][0] == 7);
    CHECK(completed->attr[GX_VA_TEX0].value_words[1][0] == 8);
    CHECK(completed->attr[GX_VA_TEX0].value_words[2][0] == 9);
    CHECK(completed->attr[GX_VA_TEX0].value_words[0][1] == 0);
    CHECK(completed->attr[GX_VA_TEX0].value_words[1][1] == 0);
    CHECK(completed->attr[GX_VA_TEX0].value_words[2][1] == 0);

    reset_state();
    configure_direct_tex_s(GX_S8);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXTexCoord1s8((s8)-3, (s8)101);
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXTexCoord1s8((s8)-2, (s8)102);
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXTexCoord1s8((s8)-1, (s8)103);
    CHECK(g_gx.current_vertex.texcoord[0][1] == 103.0f);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 1);
    CHECK(completed->attr[GX_VA_TEX0].value_words[0][0] == UINT32_C(0xFD));
    CHECK(completed->attr[GX_VA_TEX0].value_words[1][0] == UINT32_C(0xFE));
    CHECK(completed->attr[GX_VA_TEX0].value_words[2][0] == UINT32_C(0xFF));
    CHECK(completed->attr[GX_VA_TEX0].value_words[0][1] == 0);
    CHECK(completed->attr[GX_VA_TEX0].value_words[1][1] == 0);
    CHECK(completed->attr[GX_VA_TEX0].value_words[2][1] == 0);
    return 0;
}

static int test_index_width_mismatch(void) {
    float positions[3][3] = {
        {10.0f, 11.0f, 12.0f},
        {20.0f, 21.0f, 22.0f},
        {30.0f, 31.0f, 32.0f}
    };
    float texcoords[3][2] = {
        {0.0f, 1.0f},
        {2.0f, 3.0f},
        {4.0f, 5.0f}
    };
    const PCGXRawGeometryBatch* completed;

    /* GXPosition1x8 must not satisfy a GX_INDEX16 descriptor. */
    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX16);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetArray(GX_VA_POS, positions, sizeof(positions), sizeof(positions[0]));
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition1x8(0);
    GXPosition1x8(1);
    GXPosition1x8(2);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 0);
    CHECK(completed->invalid != 0);
    CHECK(completed->attr[GX_VA_POS].index_count == 3);
    CHECK(completed->attr[GX_VA_POS].index_known[0] == 0);

    /* GXTexCoord1x16 must not satisfy a GX_INDEX8 descriptor; this exercises
     * the same width check through a non-position attribute. */
    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxDesc(GX_VA_TEX0, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetArray(GX_VA_TEX0, texcoords, sizeof(texcoords), sizeof(texcoords[0]));
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXTexCoord1x16(0);
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXTexCoord1x16(1);
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXTexCoord1x16(2);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 0);
    CHECK(completed->invalid != 0);
    CHECK(completed->attr[GX_VA_TEX0].index_count == 3);
    CHECK(completed->attr[GX_VA_TEX0].index_known[0] == 0);
    return 0;
}

static int test_fail_closed_inputs(void) {
    float positions[3][3] = {{0, 0, 0}, {1, 1, 1}, {2, 2, 2}};
    const PCGXRawGeometryBatch* completed;

    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_NRM, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_NBT3, GX_F32, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    emit_direct_triangle();
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 0);
    CHECK(completed->invalid != 0);

    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_NBT, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NBT, GX_NRM_NBT, GX_S8, 0);
    GXSetArray(GX_VA_NBT, positions, sizeof(positions), sizeof(positions[0]));
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    emit_direct_triangle();
    GXEnd();
    CHECK(geometry_shadow()->completed.known == 0);
    CHECK(geometry_shadow()->completed.invalid != 0);

    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetArray(GX_VA_POS, positions, sizeof(positions[0]), sizeof(positions[0]));
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition1x8(1);
    GXPosition1x8(1);
    GXPosition1x8(1);
    GXEnd();
    CHECK(geometry_shadow()->completed.known == 0);
    CHECK(geometry_shadow()->completed.invalid != 0);
    return 0;
}

static int test_mid_begin_mutation_policy(void) {
    float positions[3][3] = {
        {1.0f, 2.0f, 3.0f},
        {4.0f, 5.0f, 6.0f},
        {7.0f, 8.0f, 9.0f}
    };
    const PCGXRawGeometryBatch* completed;
    uint64_t first_generation;

    reset_state();
    configure_direct_position();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(positions[0][0], positions[0][1], positions[0][2]);
    GXSetVtxDesc(GX_VA_POS, GX_NONE);
    GXPosition3f32(positions[1][0], positions[1][1], positions[1][2]);
    GXPosition3f32(positions[2][0], positions[2][1], positions[2][2]);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 0);
    CHECK(completed->invalid != 0);
    CHECK(g_gx.vtx_desc[GX_VA_POS] == GX_NONE);

    reset_state();
    configure_direct_position();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_U8, 0);
    emit_direct_triangle();
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 0);
    CHECK(completed->invalid != 0);
    CHECK(geometry_shadow()->format[GX_VTXFMT0][GX_VA_POS].vat_type == GX_U8);

    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetArray(GX_VA_POS, positions, sizeof(positions), sizeof(positions[0]));
    first_generation = geometry_shadow()->array[GX_VA_POS].generation;
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition1x8(0);
    GXSetArray(GX_VA_POS, positions, sizeof(positions), sizeof(positions[0]));
    GXPosition1x8(1);
    GXPosition1x8(2);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 0);
    CHECK(completed->invalid != 0);
    CHECK(completed->attr[GX_VA_POS].array_generation == first_generation);
    CHECK(geometry_shadow()->array[GX_VA_POS].generation > first_generation);
    CHECK(g_gx.array_base[GX_VA_POS] == positions);
    return 0;
}

static int test_unsupported_attribute_slots(void) {
    static const uint32_t unsupported[] = {
        GX_VA_PNMTXIDX,
        GX_VA_CLR1,
        GX_VA_TEX1,
        GX_POS_MTX_ARRAY,
        GX_VA_NBT
    };
    size_t i;

    for (i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++) {
        reset_state();
        configure_direct_position();
        GXSetVtxDesc(unsupported[i], GX_DIRECT);
        GXSetVtxAttrFmt(
            GX_VTXFMT0,
            unsupported[i],
            unsupported[i] == GX_VA_NBT ? GX_NRM_NBT3 : GX_POS_XYZ,
            GX_F32,
            0
        );
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        emit_direct_triangle();
        GXEnd();
        CHECK(geometry_shadow()->completed.known == 0);
        CHECK(geometry_shadow()->completed.invalid != 0);
    }
    return 0;
}

static int test_finite_value_validation(void) {
    float indexed_positions[3][3] = {
        {NAN, 2.0f, 3.0f},
        {4.0f, 5.0f, 6.0f},
        {7.0f, 8.0f, 9.0f}
    };
    const PCGXRawGeometryBatch* completed;

    reset_state();
    configure_direct_position();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(NAN, 2.0f, 3.0f);
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 0);
    CHECK(completed->invalid != 0);

    reset_state();
    configure_direct_position();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, INFINITY, 3.0f);
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 0);
    CHECK(completed->invalid != 0);

    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetArray(
        GX_VA_POS,
        indexed_positions,
        sizeof(indexed_positions),
        sizeof(indexed_positions[0])
    );
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition1x8(0);
    GXPosition1x8(1);
    GXPosition1x8(2);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 0);
    CHECK(completed->invalid != 0);
    return 0;
}

static int test_array_interval_validation(void) {
    float positions[3][3] = {
        {10.0f, 11.0f, 12.0f},
        {20.0f, 21.0f, 22.0f},
        {30.0f, 31.0f, 32.0f}
    };
    const PCGXRawGeometryBatch* completed;

    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetArray(GX_VA_POS, positions, sizeof(positions), sizeof(float));
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition1x8(0);
    GXPosition1x8(1);
    GXPosition1x8(2);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 0);
    CHECK(completed->invalid != 0);

    reset_state();
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetArray(GX_VA_POS, positions, sizeof(positions[0]), sizeof(positions[0]));
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition1x8(1);
    GXPosition1x8(1);
    GXPosition1x8(1);
    GXEnd();
    completed = &geometry_shadow()->completed;
    CHECK(completed->known == 0);
    CHECK(completed->invalid != 0);
    return 0;
}

static int test_completed_copy_lifetime(void) {
    const PCGXRawGeometryBatch* first;
    uint32_t first_word;

    reset_state();
    configure_direct_position();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(1.0f, 2.0f, 3.0f);
    GXPosition3f32(4.0f, 5.0f, 6.0f);
    GXPosition3f32(7.0f, 8.0f, 9.0f);
    GXEnd();
    first = &geometry_shadow()->completed;
    first_word = first->attr[GX_VA_POS].value_words[0][0];

    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(10.0f, 20.0f, 30.0f);
    GXPosition3f32(40.0f, 50.0f, 60.0f);
    GXPosition3f32(70.0f, 80.0f, 90.0f);
    GXEnd();
    CHECK(first == &geometry_shadow()->completed);
    CHECK(first->attr[GX_VA_POS].value_words[0][0] !=
          first_word);
    CHECK(first_word == UINT32_C(0x3F800000));
    CHECK(first->attr[GX_VA_POS].value_words[0][0] ==
          UINT32_C(0x41200000));
    return 0;
}

int main(void) {
    CHECK(test_initial_unknownness() == 0);
    CHECK(test_indexed_position_host_scalar_forms() == 0);
    CHECK(test_indexed_texcoord_host_scalar_forms() == 0);
    CHECK(test_indexed_normal_host_scalar_forms() == 0);
    CHECK(test_indexed_host_fallback_policy() == 0);
    CHECK(test_direct_packed_color_forms() == 0);
    CHECK(test_indexed_rgbx8_ignored_x() == 0);
    CHECK(test_temporal_order_and_immutable_direct_copy() == 0);
    CHECK(test_index8_provenance_and_bounds() == 0);
    CHECK(test_index16_and_generation_change() == 0);
    CHECK(test_indexed_rgba8_byte_order() == 0);
    CHECK(test_direct_tex_s_wrappers() == 0);
    CHECK(test_index_width_mismatch() == 0);
    CHECK(test_fail_closed_inputs() == 0);
    CHECK(test_mid_begin_mutation_policy() == 0);
    CHECK(test_unsupported_attribute_slots() == 0);
    CHECK(test_finite_value_validation() == 0);
    CHECK(test_array_interval_validation() == 0);
    CHECK(test_completed_copy_lifetime() == 0);
    puts("pc_gx_geometry_raw_batch_fixture: PASS");
    return 0;
}
