#include "pc_gx_internal.h"

#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXStruct.h>
#include <dolphin/gx/GXGeometry.h>
#include <dolphin/gx/GXVert.h>

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

int main(void) {
    CHECK(test_initial_unknownness() == 0);
    CHECK(test_temporal_order_and_immutable_direct_copy() == 0);
    CHECK(test_index8_provenance_and_bounds() == 0);
    CHECK(test_index16_and_generation_change() == 0);
    CHECK(test_indexed_rgba8_byte_order() == 0);
    CHECK(test_fail_closed_inputs() == 0);
    puts("pc_gx_geometry_raw_batch_fixture: PASS");
    return 0;
}
