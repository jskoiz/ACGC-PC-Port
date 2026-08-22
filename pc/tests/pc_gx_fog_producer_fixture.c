#include "pc_gx_fog_producer.h"

#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXStruct.h>

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void GXSetFog(
    u32 type,
    f32 startz,
    f32 endz,
    f32 nearz,
    f32 farz,
    GXColor color
);
extern void GXInitFogAdjTable(void* table, u16 width, f32 projmtx[4][4]);
extern void GXSetFogRangeAdj(GXBool enable, u16 center, void* table);
extern void pc_gx_raw_fog_range_fixture(
    uint32_t enable,
    u16 center,
    const void* table
);

/* The focused target links pc_gx.c without the full PC host executable. */
int g_pc_window_w = PC_SCREEN_WIDTH;
int g_pc_window_h = PC_SCREEN_HEIGHT;
int g_pc_widescreen_stretch = 0;

void pc_gx_tev_seq_reset(void) {
}

/* Keep pc_gx_init() callable without pulling renderer/TEV implementations
 * into this focused source fixture. */
void pc_gx_tev_init(void) {
}

void pc_gx_texture_init(void) {
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

static GLuint g_fixture_next_gl_object = 1;

static void fixture_gl_gen_vertex_arrays(GLsizei count, GLuint* arrays) {
    GLsizei index;

    for (index = 0; index < count; index++) {
        arrays[index] = g_fixture_next_gl_object++;
    }
}

static void fixture_gl_gen_buffers(GLsizei count, GLuint* buffers) {
    GLsizei index;

    for (index = 0; index < count; index++) {
        buffers[index] = g_fixture_next_gl_object++;
    }
}

static void fixture_gl_buffer_data(
    GLenum target,
    GLsizeiptr size,
    const void* data,
    GLenum usage
) {
    (void)target;
    (void)size;
    (void)data;
    (void)usage;
}

static void fixture_gl_enable_vertex_attrib_array(GLuint index) {
    (void)index;
}

static void fixture_gl_vertex_attrib_pointer(
    GLuint index,
    GLint size,
    GLenum type,
    GLboolean normalized,
    GLsizei stride,
    const void* pointer
) {
    (void)index;
    (void)size;
    (void)type;
    (void)normalized;
    (void)stride;
    (void)pointer;
}

static void fixture_gl_enable(GLenum cap) {
    (void)cap;
}

static void fixture_gl_depth_func(GLenum function) {
    (void)function;
}

static void fixture_gl_blend_func(GLenum source, GLenum destination) {
    (void)source;
    (void)destination;
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static const PCGXRawFog* raw_fog(void) {
    return pc_gx_raw_fog_shadow_fixture();
}

static void reset_state(void) {
    pc_gx_clear_semantic_packet_handoff();
    memset(&g_gx, 0, sizeof(g_gx));
    g_fixture_next_gl_object = 1;
    glad_glBindVertexArray = fixture_gl_bind_vertex_array;
    glad_glBindBuffer = fixture_gl_bind_buffer;
    glad_glGenVertexArrays = fixture_gl_gen_vertex_arrays;
    glad_glGenBuffers = fixture_gl_gen_buffers;
    glad_glBufferData = fixture_gl_buffer_data;
    glad_glEnableVertexAttribArray = fixture_gl_enable_vertex_attrib_array;
    glad_glVertexAttribPointer = fixture_gl_vertex_attrib_pointer;
    glad_glEnable = fixture_gl_enable;
    glad_glDepthFunc = fixture_gl_depth_func;
    glad_glBlendFunc = fixture_gl_blend_func;

    /* These host defaults remain useful to the legacy path, but do not
     * establish setter-owned Fog provenance. */
    g_gx.fog_type = GX_FOG_NONE;
}

static f32 float_from_bits(uint32_t bits) {
    f32 value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static GXColor make_color(u8 red, u8 green, u8 blue, u8 alpha) {
    GXColor color;

    color.r = red;
    color.g = green;
    color.b = blue;
    color.a = alpha;
    return color;
}

static void set_valid_fog(void) {
    GXSetFog(
        GX_FOG_PERSP_LIN,
        float_from_bits(UINT32_C(0x3F800000)),
        float_from_bits(UINT32_C(0x40000000)),
        float_from_bits(UINT32_C(0x3F000000)),
        float_from_bits(UINT32_C(0x40A00000)),
        make_color(0x11, 0x22, 0x33, 0x44)
    );
}

static void init_valid_raw_fog(PCGXRawFog* input) {
    uint32_t index;

    memset(input, 0, sizeof(*input));
    input->value.fog_type = ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN;
    input->value.start_bits = UINT32_C(0x3F800000);
    input->value.end_bits = UINT32_C(0x40000000);
    input->value.near_bits = UINT32_C(0x3F000000);
    input->value.far_bits = UINT32_C(0x40A00000);
    input->value.color_rgba8 = UINT32_C(0x44332211);
    input->value.range_adjust_enable = 1;
    input->value.range_center = 320;
    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        input->value.range_adjust[index] = UINT32_C(0x100) + index;
    }
    input->known_mask = PC_GX_RAW_FOG_KNOWN_ALL;
}

static void init_output_sentinel(AcgcGxCanonicalFogState* output) {
    memset(output, 0xA5, sizeof(*output));
}

static int output_is_sentinel(
    const AcgcGxCanonicalFogState* output,
    const AcgcGxCanonicalFogState* sentinel
) {
    return memcmp(output, sentinel, sizeof(*output)) == 0;
}

static int expect_fog_value(
    const PCGXRawFog* shadow,
    uint32_t fog_type,
    uint32_t start_bits,
    uint32_t end_bits,
    uint32_t near_bits,
    uint32_t far_bits,
    uint32_t color_rgba8
) {
    return shadow->known_mask == PC_GX_RAW_FOG_KNOWN_FOG &&
        shadow->invalid == 0 &&
        shadow->value.fog_type == fog_type &&
        shadow->value.start_bits == start_bits &&
        shadow->value.end_bits == end_bits &&
        shadow->value.near_bits == near_bits &&
        shadow->value.far_bits == far_bits &&
        shadow->value.color_rgba8 == color_rgba8 &&
        shadow->value.reserved[0] == 0 &&
        shadow->value.reserved[1] == 0;
}

static int expect_range_value(
    const PCGXRawFog* shadow,
    uint32_t enable,
    uint32_t center,
    const uint32_t* expected_range
) {
    uint32_t index;

    if (shadow->known_mask != PC_GX_RAW_FOG_KNOWN_ALL ||
        shadow->invalid != 0 ||
        shadow->value.range_adjust_enable != enable ||
        shadow->value.range_center != center ||
        shadow->value.reserved[0] != 0 ||
        shadow->value.reserved[1] != 0) {
        return 0;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        if (shadow->value.range_adjust[index] != expected_range[index]) {
            return 0;
        }
    }
    return 1;
}

static int raw_fog_is_zero(void) {
    PCGXRawFog zero;

    memset(&zero, 0, sizeof(zero));
    return memcmp(raw_fog(), &zero, sizeof(zero)) == 0;
}

static int test_initial_unknownness_and_failure(void) {
    PCGXRawFog input;
    AcgcGxCanonicalFogState output;
    AcgcGxCanonicalFogState sentinel;

    reset_state();
    CHECK(raw_fog() == &g_gx.raw_fog);
    CHECK(raw_fog_is_zero());

    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_fog_build_canonical(raw_fog(), &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw_fog(&input);
    input.known_mask = PC_GX_RAW_FOG_KNOWN_FOG;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_fog_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));
    CHECK(!pc_gx_raw_fog_build_canonical(&input, NULL));
    CHECK(!pc_gx_raw_fog_build_canonical(NULL, &output));
    return 0;
}

static int table_matches(
    const GXFogAdjTable* table,
    const uint16_t* expected
) {
    uint32_t index;

    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        if (table->r[index] != expected[index]) return 0;
    }
    return 1;
}

static void init_perspective_projection(f32 projection[4][4]) {
    memset(projection, 0, sizeof(f32) * 16);
    projection[0][0] = 1.0f;
    projection[2][2] = 0.0f;
    projection[2][3] = -1.0f;
    projection[3][3] = 0.0f;
}

static void init_orthographic_projection(f32 projection[4][4]) {
    memset(projection, 0, sizeof(f32) * 16);
    projection[0][0] = 1.0f;
    projection[0][3] = -1.0f;
    projection[2][2] = 1.0f;
    projection[2][3] = 0.0f;
    projection[3][3] = 1.0f;
}

static int expect_init_table_no_write(
    u16 width,
    f32 projection[4][4]
) {
    GXFogAdjTable table;
    GXFogAdjTable before_table;
    PCGXRawFog before_raw;

    reset_state();
    memset(&table, 0xA5, sizeof(table));
    before_table = table;
    before_raw = *raw_fog();
    GXInitFogAdjTable(&table, width, projection);
    return memcmp(&table, &before_table, sizeof(table)) == 0 &&
        memcmp(raw_fog(), &before_raw, sizeof(before_raw)) == 0;
}

static int test_init_table_formula_and_guards(void) {
    static const uint16_t perspective_expected[] = {
        257, 261, 267, 275, 286,
        298, 312, 327, 344, 362
    };
    static const uint16_t orthographic_expected[] = {
        275, 327, 399, 483, 572,
        665, 761, 858, 956, 1055
    };
    GXFogAdjTable table;
    GXFogAdjTable before_table;
    PCGXRawFog before_raw;
    f32 projection[4][4];

    CHECK(sizeof(table.r) ==
          ACGC_GX_CANONICAL_FOG_RANGE_COUNT * sizeof(u16));

    reset_state();
    init_perspective_projection(projection);
    memset(&table, 0xA5, sizeof(table));
    before_raw = *raw_fog();
    GXInitFogAdjTable(&table, 640, projection);
    CHECK(table_matches(&table, perspective_expected));
    CHECK(memcmp(raw_fog(), &before_raw, sizeof(before_raw)) == 0);

    reset_state();
    init_perspective_projection(projection);
    projection[1][1] = NAN;
    projection[3][0] = INFINITY;
    memset(&table, 0xA5, sizeof(table));
    GXInitFogAdjTable(&table, 640, projection);
    CHECK(table_matches(&table, perspective_expected));

    reset_state();
    init_orthographic_projection(projection);
    memset(&table, 0xA5, sizeof(table));
    before_raw = *raw_fog();
    GXInitFogAdjTable(&table, 320, projection);
    CHECK(table_matches(&table, orthographic_expected));
    CHECK(memcmp(raw_fog(), &before_raw, sizeof(before_raw)) == 0);

    init_perspective_projection(projection);
    CHECK(expect_init_table_no_write(0, projection));
    CHECK(expect_init_table_no_write(641, projection));

    reset_state();
    memset(&table, 0xA5, sizeof(table));
    before_table = table;
    before_raw = *raw_fog();
    GXInitFogAdjTable(&table, 640, NULL);
    CHECK(memcmp(&table, &before_table, sizeof(table)) == 0);
    CHECK(memcmp(raw_fog(), &before_raw, sizeof(before_raw)) == 0);

    reset_state();
    init_perspective_projection(projection);
    before_raw = *raw_fog();
    GXInitFogAdjTable(NULL, 640, projection);
    CHECK(memcmp(raw_fog(), &before_raw, sizeof(before_raw)) == 0);

    init_perspective_projection(projection);
    projection[0][0] = 0.0f;
    CHECK(expect_init_table_no_write(640, projection));

    init_perspective_projection(projection);
    projection[2][2] = 1.0f;
    CHECK(expect_init_table_no_write(640, projection));

    init_perspective_projection(projection);
    projection[0][2] = NAN;
    CHECK(expect_init_table_no_write(640, projection));

    init_perspective_projection(projection);
    projection[0][2] = FLT_MAX;
    CHECK(expect_init_table_no_write(1, projection));

    init_perspective_projection(projection);
    projection[0][0] = 1.0e-9f;
    CHECK(expect_init_table_no_write(1, projection));
    return 0;
}

static int test_exact_fog_bits_and_copied_range(void) {
    static const uint16_t table[] = {
        0x100, 0x101, 0x102, 0x103, 0x104,
        0x105, 0x106, 0x107, 0x108, 0x109
    };
    uint16_t caller_table[ACGC_GX_CANONICAL_FOG_RANGE_COUNT];
    uint8_t unaligned_storage[1 + sizeof(table)];
    uint32_t expected_range[ACGC_GX_CANONICAL_FOG_RANGE_COUNT];
    AcgcGxCanonicalFogState output;
    uint32_t index;

    reset_state();
    set_valid_fog();
    CHECK(expect_fog_value(
        raw_fog(),
        GX_FOG_PERSP_LIN,
        UINT32_C(0x3F800000),
        UINT32_C(0x40000000),
        UINT32_C(0x3F000000),
        UINT32_C(0x40A00000),
        UINT32_C(0x44332211)
    ));
    CHECK(g_gx.fog_type == GX_FOG_PERSP_LIN);
    CHECK((g_gx.dirty & PC_GX_DIRTY_FOG) != 0);

    init_output_sentinel(&output);
    CHECK(!pc_gx_raw_fog_build_canonical(raw_fog(), &output));

    memcpy(caller_table, table, sizeof(caller_table));
    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        expected_range[index] = table[index];
    }
    GXSetFogRangeAdj(GX_TRUE, 320, caller_table);
    CHECK(expect_range_value(raw_fog(), 1, 320, expected_range));
    CHECK(pc_gx_raw_fog_build_canonical(raw_fog(), &output));
    CHECK(acgc_gx_canonical_fog_state_validate(&output));

    /* The raw sideband owns a copy, never the caller's table pointer. */
    memset(caller_table, 0, sizeof(caller_table));
    CHECK(expect_range_value(raw_fog(), 1, 320, expected_range));
    CHECK(output.range_adjust[0] == expected_range[0]);

    memcpy(unaligned_storage + 1, table, sizeof(table));
    GXSetFogRangeAdj(GX_TRUE, 320, unaligned_storage + 1);
    CHECK(expect_range_value(raw_fog(), 1, 320, expected_range));

    /* The decomp does not rewrite range entries when disabling adjustment. */
    GXSetFogRangeAdj(GX_FALSE, UINT16_C(65535), NULL);
    CHECK(expect_range_value(raw_fog(), 0, UINT16_C(65535), expected_range));
    CHECK(pc_gx_raw_fog_build_canonical(raw_fog(), &output));
    CHECK(output.range_adjust[9] == expected_range[9]);
    return 0;
}

static int test_valid_types_and_inactive_bits(void) {
    static const uint32_t fog_types[] = {
        ACGC_GX_CANONICAL_FOG_TYPE_NONE,
        ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN,
        ACGC_GX_CANONICAL_FOG_TYPE_PERSP_EXP,
        ACGC_GX_CANONICAL_FOG_TYPE_PERSP_EXP2,
        ACGC_GX_CANONICAL_FOG_TYPE_PERSP_REVEXP,
        ACGC_GX_CANONICAL_FOG_TYPE_PERSP_REVEXP2,
        ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_LIN,
        ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_EXP,
        ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_EXP2,
        ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_REVEXP,
        ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_REVEXP2
    };
    AcgcGxCanonicalFogState output;
    size_t index;

    for (index = 0; index < sizeof(fog_types) / sizeof(fog_types[0]); index++) {
        reset_state();
        if (fog_types[index] == ACGC_GX_CANONICAL_FOG_TYPE_NONE) {
            GXSetFog(
                fog_types[index],
                float_from_bits(UINT32_C(0x7FC00001)),
                float_from_bits(UINT32_C(0xFF800000)),
                float_from_bits(UINT32_C(0x7F800000)),
                float_from_bits(UINT32_C(0x7FC00002)),
                make_color(1, 2, 3, 4)
            );
        } else {
            GXSetFog(
                fog_types[index],
                float_from_bits(UINT32_C(0x3F800000)),
                float_from_bits(UINT32_C(0x40000000)),
                float_from_bits(UINT32_C(0x3F000000)),
                float_from_bits(UINT32_C(0x40A00000)),
                make_color(1, 2, 3, 4)
            );
        }
        GXSetFogRangeAdj(GX_FALSE, 0, NULL);
        CHECK(pc_gx_raw_fog_build_canonical(raw_fog(), &output));
        CHECK(output.fog_type == fog_types[index]);
        CHECK(acgc_gx_canonical_fog_state_validate(&output));
    }
    return 0;
}

static int test_setter_float_edge_bits(void) {
    AcgcGxCanonicalFogState output;

    reset_state();
    GXSetFog(
        GX_FOG_PERSP_LIN,
        float_from_bits(UINT32_C(0x80000000)),
        float_from_bits(UINT32_C(0x80000000)),
        float_from_bits(UINT32_C(0x00000001)),
        float_from_bits(UINT32_C(0x00000001)),
        make_color(1, 2, 3, 4)
    );
    GXSetFogRangeAdj(GX_FALSE, 0, NULL);
    CHECK(pc_gx_raw_fog_build_canonical(raw_fog(), &output));
    CHECK(output.start_bits == UINT32_C(0x80000000));
    CHECK(output.end_bits == UINT32_C(0x80000000));
    CHECK(output.near_bits == UINT32_C(0x00000001));
    CHECK(output.far_bits == UINT32_C(0x00000001));
    CHECK(acgc_gx_canonical_fog_state_validate(&output));
    return 0;
}

static int expect_invalid_fog_setter(
    u32 type,
    f32 startz,
    f32 endz,
    f32 nearz,
    f32 farz
) {
    PCGXRawFog before;
    GXColor color = make_color(0x11, 0x22, 0x33, 0x44);

    reset_state();
    set_valid_fog();
    before = *raw_fog();
    g_gx.dirty = 0;

    GXSetFog(type, startz, endz, nearz, farz, color);
    CHECK(raw_fog()->invalid == 1);
    CHECK(raw_fog()->known_mask == before.known_mask);
    CHECK(memcmp(&raw_fog()->value, &before.value,
                 sizeof(before.value)) == 0);
    CHECK(g_gx.fog_type == (int)type);
    CHECK((g_gx.dirty & PC_GX_DIRTY_FOG) != 0);

    GXSetFog(
        GX_FOG_PERSP_LIN,
        1.0f, 2.0f, 0.5f, 5.0f,
        color
    );
    CHECK(raw_fog()->invalid == 1);
    CHECK(memcmp(&raw_fog()->value, &before.value,
                 sizeof(before.value)) == 0);
    CHECK(g_gx.fog_type == GX_FOG_PERSP_LIN);
    return 0;
}

static int test_sticky_invalidity_and_legacy_fog(void) {
    static const u32 rejected_types[] = { 8, 9, 11, 16, UINT32_MAX };
    static const uint32_t invalid_type = 1;
    static const f32 valid_start = 1.0f;
    static const f32 valid_end = 2.0f;
    static const f32 valid_near = 0.5f;
    size_t index;

    CHECK(expect_invalid_fog_setter(
        invalid_type,
        valid_start,
        valid_end,
        valid_near,
        5.0f
    ) == 0);
    CHECK(expect_invalid_fog_setter(
        GX_FOG_PERSP_LIN,
        valid_start,
        valid_end,
        valid_near,
        -1.0f
    ) == 0);
    CHECK(expect_invalid_fog_setter(
        GX_FOG_PERSP_LIN,
        valid_start,
        valid_end,
        6.0f,
        5.0f
    ) == 0);
    for (index = 0; index < sizeof(rejected_types) / sizeof(rejected_types[0]); index++) {
        CHECK(expect_invalid_fog_setter(
            rejected_types[index],
            valid_start,
            valid_end,
            valid_near,
            5.0f
        ) == 0);
    }
    return 0;
}

static int test_pc_gx_init_resets_sticky_invalidity(void) {
    AcgcGxCanonicalFogState output;

    reset_state();
    set_valid_fog();
    GXSetFogRangeAdj(GX_FALSE, 0, NULL);
    GXSetFog(
        GX_FOG_PERSP_LIN,
        1.0f, 2.0f, 0.5f, -1.0f,
        make_color(0x11, 0x22, 0x33, 0x44)
    );
    CHECK(raw_fog()->invalid == 1);

    pc_gx_init();
    CHECK(raw_fog_is_zero());
    CHECK(g_gx.in_begin == 0);

    set_valid_fog();
    GXSetFogRangeAdj(GX_FALSE, 0, NULL);
    CHECK(pc_gx_raw_fog_build_canonical(raw_fog(), &output));
    CHECK(acgc_gx_canonical_fog_state_validate(&output));
    return 0;
}

static int expect_invalid_range_setter(
    GXBool enable,
    u16 center,
    void* table
) {
    static const uint16_t valid_table[] = {
        0x100, 0x101, 0x102, 0x103, 0x104,
        0x105, 0x106, 0x107, 0x108, 0x109
    };
    PCGXRawFog before;

    reset_state();
    set_valid_fog();
    GXSetFogRangeAdj(GX_FALSE, 0, NULL);
    before = *raw_fog();
    GXSetFogRangeAdj(enable, center, table);
    CHECK(raw_fog()->invalid == 1);
    CHECK(raw_fog()->known_mask == before.known_mask);
    CHECK(memcmp(&raw_fog()->value, &before.value,
                 sizeof(before.value)) == 0);

    GXSetFogRangeAdj(GX_TRUE, 320, (void*)valid_table);
    CHECK(raw_fog()->invalid == 1);
    CHECK(memcmp(&raw_fog()->value, &before.value,
                 sizeof(before.value)) == 0);
    return 0;
}

static int test_range_domain_sticky_invalidity(void) {
    static uint16_t invalid_table[ACGC_GX_CANONICAL_FOG_RANGE_COUNT] = {
        0x100, 0x101, 0x102, 0x103, 0x104,
        0x105, 0x106, 0x107, 0x108, 0x1000
    };

    CHECK(expect_invalid_range_setter(GX_TRUE, 320, invalid_table) == 0);
    CHECK(expect_invalid_range_setter(GX_TRUE, 682, invalid_table) == 0);
    CHECK(expect_invalid_range_setter(GX_TRUE, 320, NULL) == 0);
    return 0;
}

static int test_rejected_gx_bool_domain(void) {
    PCGXRawFog before;

    reset_state();
    set_valid_fog();
    GXSetFogRangeAdj(GX_FALSE, 0, NULL);
    before = *raw_fog();
    /* The invalid GXBool domain is rejected before the table is inspected. */
    pc_gx_raw_fog_range_fixture(2, 320, NULL);
    CHECK(raw_fog()->invalid == 1);
    CHECK(raw_fog()->known_mask == before.known_mask);
    CHECK(memcmp(&raw_fog()->value, &before.value,
                 sizeof(before.value)) == 0);
    return 0;
}

static int test_producer_rejects_malformed_values(void) {
    PCGXRawFog input;
    AcgcGxCanonicalFogState output;
    AcgcGxCanonicalFogState sentinel;
    uint32_t index;

    init_valid_raw_fog(&input);
    input.known_mask = PC_GX_RAW_FOG_KNOWN_FOG;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_fog_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw_fog(&input);
    input.invalid = 1;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_fog_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw_fog(&input);
    input.value.reserved[0] = 1;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_fog_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw_fog(&input);
    input.value.fog_type = 1;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_fog_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw_fog(&input);
    input.value.start_bits = UINT32_C(0x7F800000);
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_fog_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw_fog(&input);
    input.value.far_bits = UINT32_C(0xBF800000);
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_fog_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw_fog(&input);
    input.value.range_adjust_enable = 1;
    input.value.range_center = ACGC_GX_CANONICAL_FOG_CENTER_MAX + 1;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_fog_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    init_valid_raw_fog(&input);
    input.value.range_adjust[4] = ACGC_GX_CANONICAL_FOG_RANGE_VALUE_MASK + 1;
    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_fog_build_canonical(&input, &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    /* Inactive Fog preserves its exact non-finite parameter words, while a
     * disabled range keeps the widened source center and bounded table. */
    init_valid_raw_fog(&input);
    input.value.fog_type = ACGC_GX_CANONICAL_FOG_TYPE_NONE;
    input.value.start_bits = UINT32_C(0x7FC00001);
    input.value.end_bits = UINT32_C(0xFF800000);
    input.value.near_bits = UINT32_C(0x7F800000);
    input.value.far_bits = UINT32_C(0x7FC00002);
    input.value.range_adjust_enable = 0;
    input.value.range_center = ACGC_GX_CANONICAL_FOG_CENTER_SOURCE_MAX;
    init_output_sentinel(&output);
    CHECK(pc_gx_raw_fog_build_canonical(&input, &output));
    CHECK(output.fog_type == ACGC_GX_CANONICAL_FOG_TYPE_NONE);
    CHECK(output.start_bits == UINT32_C(0x7FC00001));
    CHECK(output.range_center == ACGC_GX_CANONICAL_FOG_CENTER_SOURCE_MAX);
    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        CHECK(output.range_adjust[index] == UINT32_C(0x100) + index);
    }
    return 0;
}

static int test_success_does_not_mutate_input(void) {
    PCGXRawFog input;
    PCGXRawFog before;
    AcgcGxCanonicalFogState first;
    AcgcGxCanonicalFogState second;

    init_valid_raw_fog(&input);
    before = input;
    CHECK(pc_gx_raw_fog_build_canonical(&input, &first));
    CHECK(memcmp(&input, &before, sizeof(input)) == 0);
    init_output_sentinel(&second);
    CHECK(pc_gx_raw_fog_build_canonical(&input, &second));
    CHECK(memcmp(&first, &second, sizeof(first)) == 0);
    return 0;
}

typedef struct {
    int calls;
    int in_begin;
    int current_vertex_idx;
    int pending_verts;
    PCGXRawFog before;
    const GXFogAdjTable* watched_table;
    uint16_t table_before[ACGC_GX_CANONICAL_FOG_RANGE_COUNT];
} FogFlushObservation;

static void observe_fog_flush(
    void* context,
    const AcgcGxSemanticPacket* packet
) {
    FogFlushObservation* observation = (FogFlushObservation*)context;

    (void)packet;
    observation->calls++;
    observation->in_begin = g_gx.in_begin;
    observation->current_vertex_idx = g_gx.current_vertex_idx;
    observation->pending_verts = g_gx.pending_verts;
    observation->before = *raw_fog();
    if (observation->watched_table != NULL) {
        memcpy(
            observation->table_before,
            observation->watched_table->r,
            sizeof(observation->table_before)
        );
    }
}

static void configure_semantic_vertex_color_state(void) {
    PCGXTevStage* stage;

    g_gx.num_tev_stages = 1;
    g_gx.num_tex_gens = 0;
    g_gx.num_ind_stages = 0;
    g_gx.num_chans = 0;
    g_gx.fog_type = GX_FOG_NONE;
    g_gx.alpha_comp0 = GX_ALWAYS;
    g_gx.alpha_ref0 = 0;
    g_gx.alpha_op = GX_AOP_AND;
    g_gx.alpha_comp1 = GX_ALWAYS;
    g_gx.alpha_ref1 = 0;
    g_gx.current_mtx = 0;

    stage = &g_gx.tev_stages[0];
    stage->color_a = GX_CC_ZERO;
    stage->color_b = GX_CC_ZERO;
    stage->color_c = GX_CC_ZERO;
    stage->color_d = GX_CC_RASC;
    stage->alpha_a = GX_CA_ZERO;
    stage->alpha_b = GX_CA_ZERO;
    stage->alpha_c = GX_CA_ZERO;
    stage->alpha_d = GX_CA_RASA;
    stage->color_op = GX_TEV_ADD;
    stage->color_bias = GX_TB_ZERO;
    stage->color_scale = GX_CS_SCALE_1;
    stage->color_clamp = GX_TRUE;
    stage->color_out = GX_TEVPREV;
    stage->alpha_op = GX_TEV_ADD;
    stage->alpha_bias = GX_TB_ZERO;
    stage->alpha_scale = GX_CS_SCALE_1;
    stage->alpha_clamp = GX_TRUE;
    stage->alpha_out = GX_TEVPREV;
    stage->tex_coord = GX_TEXCOORD_NULL;
    stage->tex_map = GX_TEXMAP_NULL;
    stage->color_chan = GX_COLOR0A0;
    stage->ras_swap = GX_TEV_SWAP0;
    stage->tex_swap = GX_TEV_SWAP0;
}

static void prepare_completed_batch(FogFlushObservation* observation) {
    memset(observation, 0, sizeof(*observation));
    g_gx.in_begin = 1;
    g_gx.expected_vertex_count = 3;
    g_gx.current_vertex_idx = 3;
    g_gx.pending_verts = 0;
    g_gx.vertex_pending = 0;
    g_gx.current_primitive = GX_TRIANGLES;
    g_gx.pending_prim = GX_TRIANGLES;
    pc_gx_set_semantic_packet_handoff(observe_fog_flush, observation);
}

static void prepare_incomplete_batch(void) {
    g_gx.in_begin = 1;
    g_gx.expected_vertex_count = 3;
    g_gx.current_vertex_idx = 1;
    g_gx.pending_verts = 0;
    g_gx.vertex_pending = 0;
    g_gx.current_primitive = GX_TRIANGLES;
    g_gx.pending_prim = GX_TRIANGLES;
}

typedef struct {
    int fog_type;
    f32 fog_start;
    f32 fog_end;
    f32 fog_near;
    f32 fog_far;
    f32 fog_color[4];
    uint32_t dirty;
} FogHostSentinel;

static FogHostSentinel capture_fog_host_sentinel(void) {
    FogHostSentinel sentinel;

    sentinel.fog_type = g_gx.fog_type;
    sentinel.fog_start = g_gx.fog_start;
    sentinel.fog_end = g_gx.fog_end;
    sentinel.fog_near = g_gx.fog_near;
    sentinel.fog_far = g_gx.fog_far;
    memcpy(sentinel.fog_color, g_gx.fog_color, sizeof(sentinel.fog_color));
    sentinel.dirty = (uint32_t)g_gx.dirty;
    return sentinel;
}

static int fog_host_matches_sentinel(const FogHostSentinel* sentinel) {
    return g_gx.fog_type == sentinel->fog_type &&
        g_gx.fog_start == sentinel->fog_start &&
        g_gx.fog_end == sentinel->fog_end &&
        g_gx.fog_near == sentinel->fog_near &&
        g_gx.fog_far == sentinel->fog_far &&
        memcmp(g_gx.fog_color, sentinel->fog_color,
               sizeof(sentinel->fog_color)) == 0 &&
        (uint32_t)g_gx.dirty == sentinel->dirty;
}

static int test_incomplete_fog_begin_rejects_all_mutations(void) {
    static const uint16_t range_table[] = {
        0x100, 0x101, 0x102, 0x103, 0x104,
        0x105, 0x106, 0x107, 0x108, 0x109
    };
    GXFogAdjTable table;
    GXFogAdjTable before_table;
    PCGXRawFog before_raw;
    FogHostSentinel before_host;
    AcgcGxCanonicalFogState output;
    AcgcGxCanonicalFogState sentinel;
    f32 projection[4][4];

    reset_state();
    memset(&g_gx.raw_fog, 0xA5, sizeof(g_gx.raw_fog));
    g_gx.fog_type = 0x1234;
    g_gx.fog_start = -11.0f;
    g_gx.fog_end = 22.0f;
    g_gx.fog_near = -33.0f;
    g_gx.fog_far = 44.0f;
    g_gx.fog_color[0] = 0.125f;
    g_gx.fog_color[1] = 0.25f;
    g_gx.fog_color[2] = 0.5f;
    g_gx.fog_color[3] = 0.75f;
    g_gx.dirty = PC_GX_DIRTY_FOG | PC_GX_DIRTY_BLEND;
    before_raw = *raw_fog();
    before_host = capture_fog_host_sentinel();
    memset(&table, 0xA5, sizeof(table));
    before_table = table;
    init_perspective_projection(projection);
    prepare_incomplete_batch();

    GXSetFogRangeAdj(GX_TRUE, 320, NULL);
    CHECK(g_gx.in_begin != 0);
    CHECK(memcmp(raw_fog(), &before_raw, sizeof(before_raw)) == 0);
    CHECK(fog_host_matches_sentinel(&before_host));

    init_output_sentinel(&output);
    sentinel = output;
    CHECK(!pc_gx_raw_fog_build_canonical(raw_fog(), &output));
    CHECK(output_is_sentinel(&output, &sentinel));

    GXSetFog(
        GX_FOG_PERSP_LIN,
        1.0f, 2.0f, 0.5f, 5.0f,
        make_color(0x11, 0x22, 0x33, 0x44)
    );
    CHECK(g_gx.in_begin != 0);
    CHECK(memcmp(raw_fog(), &before_raw, sizeof(before_raw)) == 0);
    CHECK(fog_host_matches_sentinel(&before_host));

    GXSetFogRangeAdj(GX_TRUE, 320, (void*)range_table);
    CHECK(g_gx.in_begin != 0);
    CHECK(memcmp(raw_fog(), &before_raw, sizeof(before_raw)) == 0);
    CHECK(fog_host_matches_sentinel(&before_host));

    GXInitFogAdjTable(&table, 640, projection);
    CHECK(g_gx.in_begin != 0);
    CHECK(memcmp(raw_fog(), &before_raw, sizeof(before_raw)) == 0);
    CHECK(fog_host_matches_sentinel(&before_host));
    CHECK(memcmp(&table, &before_table, sizeof(table)) == 0);
    return 0;
}

static int test_flush_precedes_fog_mutation(void) {
    static uint16_t table[ACGC_GX_CANONICAL_FOG_RANGE_COUNT] = {
        0x100, 0x101, 0x102, 0x103, 0x104,
        0x105, 0x106, 0x107, 0x108, 0x109
    };
    FogFlushObservation observation;

    reset_state();
    configure_semantic_vertex_color_state();
    GXSetFog(
        GX_FOG_NONE,
        0.0f, 1.0f, 0.1f, 1.0f,
        make_color(0, 0, 0, 0)
    );
    GXSetFogRangeAdj(GX_FALSE, 0, NULL);
    g_gx.dirty = 0;
    prepare_completed_batch(&observation);
    GXSetFog(
        GX_FOG_PERSP_LIN,
        2.0f, 3.0f, 1.0f, 6.0f,
        make_color(4, 3, 2, 1)
    );
    pc_gx_clear_semantic_packet_handoff();

    CHECK(observation.calls == 1);
    CHECK(observation.in_begin == 0);
    CHECK(observation.current_vertex_idx == 3);
    CHECK(observation.pending_verts == 0);
    CHECK(observation.before.known_mask == PC_GX_RAW_FOG_KNOWN_ALL);
    CHECK(observation.before.value.fog_type == GX_FOG_NONE);
    CHECK(observation.before.value.start_bits == UINT32_C(0x00000000));
    CHECK(raw_fog()->value.start_bits == UINT32_C(0x40000000));
    CHECK(raw_fog()->value.color_rgba8 == UINT32_C(0x01020304));
    CHECK(g_gx.pending_verts == 3);
    CHECK((g_gx.dirty & PC_GX_DIRTY_FOG) != 0);

    reset_state();
    configure_semantic_vertex_color_state();
    GXSetFog(
        GX_FOG_NONE,
        0.0f, 1.0f, 0.1f, 1.0f,
        make_color(0, 0, 0, 0)
    );
    GXSetFogRangeAdj(GX_FALSE, 0, NULL);
    g_gx.dirty = 0;
    prepare_completed_batch(&observation);
    GXSetFogRangeAdj(GX_TRUE, 320, table);
    pc_gx_clear_semantic_packet_handoff();

    CHECK(observation.calls == 1);
    CHECK(observation.before.value.range_adjust_enable == 0);
    CHECK(raw_fog()->value.range_adjust_enable == 1);
    CHECK(raw_fog()->value.range_center == 320);
    CHECK(g_gx.pending_verts == 3);
    return 0;
}

static int test_init_table_flush_precedes_write(void) {
    static const uint16_t expected[] = {
        257, 261, 267, 275, 286,
        298, 312, 327, 344, 362
    };
    GXFogAdjTable table;
    f32 projection[4][4];
    FogFlushObservation observation;
    uint32_t index;

    reset_state();
    configure_semantic_vertex_color_state();
    init_perspective_projection(projection);
    memset(&table, 0xA5, sizeof(table));
    prepare_completed_batch(&observation);
    observation.watched_table = &table;

    GXInitFogAdjTable(&table, 640, projection);
    pc_gx_clear_semantic_packet_handoff();

    CHECK(observation.calls == 1);
    CHECK(observation.in_begin == 0);
    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        CHECK(observation.table_before[index] == UINT16_C(0xA5A5));
    }
    CHECK(table_matches(&table, expected));
    return 0;
}

int main(void) {
    if (test_initial_unknownness_and_failure() != 0 ||
        test_init_table_formula_and_guards() != 0 ||
        test_exact_fog_bits_and_copied_range() != 0 ||
        test_valid_types_and_inactive_bits() != 0 ||
        test_setter_float_edge_bits() != 0 ||
        test_sticky_invalidity_and_legacy_fog() != 0 ||
        test_pc_gx_init_resets_sticky_invalidity() != 0 ||
        test_range_domain_sticky_invalidity() != 0 ||
        test_rejected_gx_bool_domain() != 0 ||
        test_producer_rejects_malformed_values() != 0 ||
        test_success_does_not_mutate_input() != 0 ||
        test_incomplete_fog_begin_rejects_all_mutations() != 0 ||
        test_flush_precedes_fog_mutation() != 0 ||
        test_init_table_flush_precedes_write() != 0) {
        return 1;
    }

    puts("pc GX raw Fog producer fixture: PASS");
    puts("proof boundary: setter-owned CPU Fog provenance, copied FogRangeAdj data, flush ordering, and canonical validation only; no renderer, Metal, device, pixel, or playability claim");
    return 0;
}
