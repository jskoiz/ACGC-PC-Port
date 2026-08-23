#include "pc_gx_cumulative_gatherer.h"
#include "pc_gx_internal.h"
#include "pc_gx_texture_raw_state.h"
#include "pc_settings.h"
#include "pc_texture_pack.h"

#include "acgc/apple_canonical_plan.h"
#include "acgc/gx_canonical_geometry_state.h"
#include "acgc/gx_canonical_state.h"
#include "acgc/metal_packet_consumer.h"

#include <dolphin/gx/GXEnum.h>
#include <dolphin/gx/GXStruct.h>
#include <dolphin/gx/GXGeometry.h>
#include <dolphin/gx/GXVert.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The source-backed fixture links pc_gx.c without the full host executable. */
int g_pc_window_w = PC_SCREEN_WIDTH;
int g_pc_window_h = PC_SCREEN_HEIGHT;
int g_pc_widescreen_stretch = 0;
int g_pc_model_viewer_no_cull = 0;

PCSettings g_pc_settings = {
    .texture_filtering = 1
};

int pc_texture_pack_active(void) {
    return 0;
}

GLuint pc_texture_pack_lookup(
    const void* data,
    int data_size,
    int width,
    int height,
    unsigned int format,
    const void* tlut_data,
    int tlut_entries,
    int tlut_is_be,
    int* out_width,
    int* out_height
) {
    (void)data;
    (void)data_size;
    (void)width;
    (void)height;
    (void)format;
    (void)tlut_data;
    (void)tlut_entries;
    (void)tlut_is_be;
    (void)out_width;
    (void)out_height;
    return 0;
}

void pc_gx_tev_seq_reset(void) {
}

void pc_gx_tev_init(void) {
}

void pc_gx_tev_shutdown(void) {
}

static PCGXShaderVariant s_fixture_shader_variant;

PCGXShaderVariant* pc_gx_tev_get_variant(void) {
    return &s_fixture_shader_variant;
}

/* pc_gx.c intentionally keeps these ABI declarations private. */
extern void GXSetProjection(const void* matrix, u32 type);
extern void GXLoadPosMtxImm(const void* matrix, u32 id);
extern void GXSetCurrentMtx(u32 id);
extern void GXSetNumChans(u8 count);
extern void GXSetChanCtrl(
    u32 channel,
    GXBool enable,
    u32 ambient_source,
    u32 material_source,
    u32 light_mask,
    u32 diffuse_function,
    u32 attenuation_function
);
extern void GXSetChanAmbColor(u32 channel, u32 color);
extern void GXSetChanMatColor(u32 channel, u32 color);
extern void GXSetNumTevStages(u8 count);
extern void GXSetTevOp(u32 stage, u32 mode);
extern void GXSetTevOrder(u32 stage, u32 coord, u32 map, u32 color);
extern void GXSetTevColor(u32 register_id, u32 color);
extern void GXSetTevKColor(u32 register_id, u32 color);
extern void GXSetTevKColorSel(u32 stage, u32 selector);
extern void GXSetTevKAlphaSel(u32 stage, u32 selector);
extern void GXSetTevSwapMode(u32 stage, u32 ras_selector, u32 tex_selector);
extern void GXSetTevSwapModeTable(
    u32 table,
    u32 red,
    u32 green,
    u32 blue,
    u32 alpha
);
extern void GXSetTevDirect(u32 stage);
extern void GXSetNumIndStages(u8 count);
extern void GXSetAlphaCompare(
    u32 comp0,
    u8 ref0,
    u32 op,
    u32 comp1,
    u8 ref1
);
extern void GXSetBlendMode(u32 mode, u32 source, u32 destination, u32 logic);
extern void GXSetZMode(GXBool compare_enable, u32 function, GXBool update_enable);
extern void GXSetColorUpdate(GXBool enable);
extern void GXSetAlphaUpdate(GXBool enable);
extern void GXSetZCompLoc(GXBool before_texture);
extern void GXSetViewport(
    f32 left,
    f32 top,
    f32 width,
    f32 height,
    f32 nearz,
    f32 farz
);
extern void GXSetScissor(u32 left, u32 top, u32 width, u32 height);
extern void GXSetScissorBoxOffset(s32 x, s32 y);
extern void GXSetClipMode(u32 mode);
extern void GXSetCullMode(u32 mode);
extern void GXSetCoPlanar(GXBool enable);
extern void GXSetLineWidth(u8 width, u32 tex_offsets);
extern void GXSetPointSize(u8 size, u32 tex_offsets);
extern void GXEnableTexOffsets(u32 coord, GXBool line, GXBool point);
extern void GXSetDither(GXBool dither);
extern void GXSetDstAlpha(GXBool enable, u8 alpha);
extern void GXSetFieldMask(GXBool odd, GXBool even);
extern void GXSetFieldMode(GXBool field_mode, GXBool half_aspect);
extern void GXSetFog(
    u32 type,
    f32 startz,
    f32 endz,
    f32 nearz,
    f32 farz,
    GXColor color
);
extern void GXSetFogRangeAdj(GXBool enable, u16 center, void* table);
extern void GXSetNumTexGens(u8 count);

static void fixture_gl_viewport(
    GLint x,
    GLint y,
    GLsizei width,
    GLsizei height
) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void fixture_gl_depth_range(GLdouble nearz, GLdouble farz) {
    (void)nearz;
    (void)farz;
}

static void fixture_gl_enable(GLenum capability) {
    (void)capability;
}

static void fixture_gl_scissor(
    GLint x,
    GLint y,
    GLsizei width,
    GLsizei height
) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void fixture_gl_line_width(GLfloat width) {
    (void)width;
}

static void fixture_gl_point_size(GLfloat size) {
    (void)size;
}

static void install_gl_stubs(void) {
    glad_glViewport = fixture_gl_viewport;
    glad_glDepthRange = fixture_gl_depth_range;
    glad_glEnable = fixture_gl_enable;
    glad_glScissor = fixture_gl_scissor;
    glad_glLineWidth = fixture_gl_line_width;
    glad_glPointSize = fixture_gl_point_size;
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct RoundTripObservation {
    int callback_count;
    int envelope_ordered;
    size_t envelope_byte_size;
    AcgcAppleCanonicalPlan plan;
    AcgcAppleCanonicalPlanStatus plan_status;
    AcgcMetalPacketConsumerOutput output;
    AcgcMetalPacketConsumerStatus consumer_status;
} RoundTripObservation;

static RoundTripObservation s_observation;
static uint8_t s_callback_envelope_copy[PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES];
static AcgcAppleCanonicalPlan s_saved_plan;
static AcgcMetalPacketConsumerOutput s_saved_output;
static AcgcAppleCanonicalPlan s_invalid_plan;
static AcgcMetalPacketConsumerOutput s_rejection_before;
static AcgcMetalPacketConsumerOutput s_rejection_output;

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static int bytes_are_zero(const void* data, size_t byte_size) {
    const uint8_t* bytes = (const uint8_t*)data;
    size_t index;

    if (data == NULL) return 0;
    for (index = 0; index < byte_size; index++) {
        if (bytes[index] != 0) return 0;
    }
    return 1;
}

static int envelope_directory_is_ordered(
    const uint8_t* envelope,
    size_t envelope_byte_size
) {
    uint32_t index;

    if (envelope == NULL ||
        envelope_byte_size < ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET ||
        read_le32(envelope + 0) != ACGC_GX_CANONICAL_ENVELOPE_MAGIC ||
        read_le32(envelope + 4) != ACGC_GX_CANONICAL_ENVELOPE_VERSION ||
        read_le32(envelope + 8) != ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE ||
        read_le32(envelope + 12) !=
            ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE ||
        read_le32(envelope + 16) !=
            ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT ||
        read_le32(envelope + 20) !=
            ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK ||
        read_le32(envelope + 24) !=
            ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK ||
        read_le32(envelope + 28) !=
            ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK ||
        read_le32(envelope + 32) !=
            ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET ||
        read_le32(envelope + 40) != envelope_byte_size) {
        return 0;
    }

    for (index = 0; index < PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT; index++) {
        size_t offset = ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET +
            (size_t)index * ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE;

        if (read_le32(envelope + offset + 0) != index + 1 ||
            read_le32(envelope + offset + 4) !=
                ACGC_GX_CANONICAL_ENVELOPE_VERSION ||
            read_le32(envelope + offset + 16) == 0 ||
            read_le32(envelope + offset + 20) == 0 ||
            read_le32(envelope + offset + 24) == 0) {
            return 0;
        }
    }
    return 1;
}

static void observe_cumulative_envelope(
    void* context,
    const uint8_t* envelope,
    size_t envelope_byte_size
) {
    RoundTripObservation* observation = (RoundTripObservation*)context;

    observation->callback_count++;
    observation->envelope_byte_size = envelope_byte_size;
    observation->envelope_ordered = envelope_directory_is_ordered(
        envelope,
        envelope_byte_size
    );
    observation->plan_status = ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT;
    observation->consumer_status =
        ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT;
    if (envelope == NULL || envelope_byte_size > sizeof(s_callback_envelope_copy)) {
        return;
    }

    memcpy(s_callback_envelope_copy, envelope, envelope_byte_size);
    observation->plan_status = acgc_apple_canonical_plan_build(
        envelope,
        envelope_byte_size,
        &observation->plan
    );
    if (observation->plan_status != ACGC_APPLE_CANONICAL_PLAN_OK) {
        return;
    }
    observation->consumer_status =
        acgc_metal_packet_consumer_prepare_canonical_plan(
            &observation->plan,
            &observation->output
        );
}

static void configure_known_state(void) {
    static const float projection[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f}
    };
    static const float position[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f}
    };
    GXColor zero_color = {0, 0, 0, 0};
    uint32_t register_id;
    uint32_t table;
    uint32_t coord;

    GXSetProjection(projection, GX_ORTHOGRAPHIC);
    GXLoadPosMtxImm(position, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);

    GXSetNumChans(1);
    GXSetChanCtrl(
        GX_COLOR0A0,
        GX_FALSE,
        GX_SRC_REG,
        GX_SRC_REG,
        0,
        GX_DF_NONE,
        GX_AF_SPEC
    );
    GXSetChanAmbColor(GX_COLOR0A0, 0);
    GXSetChanMatColor(GX_COLOR0A0, 0);

    GXSetNumTexGens(0);

    GXSetNumTevStages(1);
    GXSetTevOp(0, GX_PASSCLR);
    /* GX_COLOR0 is the current host canonical channel value (zero). */
    GXSetTevOrder(0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0);
    GXSetTevKColorSel(0, GX_TEV_KCSEL_1);
    GXSetTevKAlphaSel(0, GX_TEV_KASEL_1);
    GXSetTevSwapMode(0, 0, 0);
    for (register_id = GX_TEVPREV;
         register_id < GX_MAX_TEVREG;
         register_id++) {
        GXSetTevColor(register_id, 0);
    }
    for (register_id = GX_KCOLOR0;
         register_id < GX_MAX_KCOLOR;
         register_id++) {
        GXSetTevKColor(register_id, 0);
    }
    for (table = 0; table < 4; table++) {
        GXSetTevSwapModeTable(table, 0, 0, 0, 0);
    }
    GXSetTevDirect(0);
    GXSetNumIndStages(0);

    GXSetBlendMode(
        GX_BM_BLEND,
        GX_BL_SRCALPHA,
        GX_BL_INVSRCALPHA,
        GX_LO_CLEAR
    );
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_TRUE);
    GXSetZCompLoc(GX_FALSE);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);

    GXSetViewport(0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f);
    GXSetScissor(0, 0, 64, 64);
    GXSetScissorBoxOffset(0, 0);
    GXSetClipMode(GX_CLIP_ENABLE);
    GXSetCullMode(GX_CULL_NONE);
    GXSetCoPlanar(GX_FALSE);
    GXSetLineWidth(0, GX_TO_ZERO);
    GXSetPointSize(0, GX_TO_ZERO);
    for (coord = 0; coord < 8; coord++) {
        GXEnableTexOffsets(coord, GX_FALSE, GX_FALSE);
    }
    GXSetDither(GX_FALSE);
    GXSetDstAlpha(GX_FALSE, 0);
    GXSetFieldMask(GX_FALSE, GX_FALSE);
    GXSetFieldMode(GX_FALSE, GX_FALSE);

    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, zero_color);
    GXSetFogRangeAdj(GX_FALSE, 0, NULL);
}

static void configure_direct_geometry(void) {
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
}

static void emit_vertices(uint32_t count) {
    static const float positions[3][3] = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}
    };
    uint32_t vertex;

    for (vertex = 0; vertex < count; vertex++) {
        GXPosition3f32(
            positions[vertex][0],
            positions[vertex][1],
            positions[vertex][2]
        );
        GXColor4u8(255, 255, 255, 255);
    }
}

static void submit_batch(uint32_t count) {
    configure_direct_geometry();
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    emit_vertices(count);
    GXEnd();
}

static int assert_successful_round_trip(int expected_callback_count) {
    const AcgcAppleCanonicalPlan* plan = &s_observation.plan;
    const AcgcAppleCanonicalPlanGeometry* geometry = &plan->geometry;
    const AcgcGxCanonicalTevStage* tev_stage = &plan->tev.stages[0];
    const uint32_t expected_present_mask =
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0);
    const uint32_t expected_component_mask =
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_POSITION |
        ACGC_APPLE_CANONICAL_PLAN_COMPONENT_COLOR0;
    const uint32_t identity[12] = {
        UINT32_C(0x3F800000), 0, 0, 0,
        0, UINT32_C(0x3F800000), 0, 0,
        0, 0, UINT32_C(0x3F800000), 0
    };
    const uint32_t expected_positions[3][3] = {
        {0, 0, 0},
        {UINT32_C(0x3F800000), 0, 0},
        {0, UINT32_C(0x3F800000), 0}
    };
    uint32_t vertex;
    uint32_t word;

    CHECK(s_observation.callback_count == expected_callback_count);
    CHECK(s_observation.envelope_ordered);
    CHECK(s_observation.envelope_byte_size >
          ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET);
    CHECK(s_observation.plan_status == ACGC_APPLE_CANONICAL_PLAN_OK);
    CHECK(s_observation.consumer_status == ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(!pc_gx_texture_raw_borrow_is_active());

    CHECK(geometry->primitive ==
          ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES);
    CHECK(geometry->vtxfmt == 0);
    CHECK(geometry->vertex_count == 3);
    CHECK(geometry->present_mask == expected_present_mask);
    CHECK(geometry->component_mask == expected_component_mask);
    CHECK(plan->transform.projection_type ==
          ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_ORTHOGRAPHIC);
    CHECK(plan->transform.projection[0] == UINT32_C(0x3F800000));
    CHECK(plan->transform.projection[1] == 0);
    CHECK(plan->transform.projection[2] == UINT32_C(0x3F800000));
    CHECK(plan->transform.projection[3] == 0);
    CHECK(plan->transform.projection[4] == UINT32_C(0x3F800000));
    CHECK(plan->transform.projection[5] == 0);
    CHECK(plan->transform.current_position_id == GX_PNMTX0);
    CHECK((plan->transform.known_mask &
           (ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK |
            ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK |
            ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(0))) ==
          (ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK |
           ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK |
           ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(0)));
    for (word = 0; word < 12; word++) {
        CHECK(plan->transform.position[0][word] == identity[word]);
    }

    for (vertex = 0; vertex < 3; vertex++) {
        const AcgcAppleCanonicalPlanVertex* plan_vertex =
            &geometry->vertices[vertex];

        CHECK(plan_vertex->present_mask == expected_present_mask);
        CHECK(plan_vertex->component_mask == expected_component_mask);
        CHECK(plan_vertex->position_matrix_id == GX_PNMTX0);
        CHECK(plan_vertex->position[0] == expected_positions[vertex][0]);
        CHECK(plan_vertex->position[1] == expected_positions[vertex][1]);
        CHECK(plan_vertex->position[2] == expected_positions[vertex][2]);
        CHECK(plan_vertex->color_rgba8[0] == UINT32_MAX);
        CHECK(plan_vertex->color_rgba8[1] == 0);
        CHECK(bytes_are_zero(plan_vertex->normal, sizeof(plan_vertex->normal)));
        CHECK(bytes_are_zero(plan_vertex->binormal, sizeof(plan_vertex->binormal)));
        CHECK(bytes_are_zero(plan_vertex->tangent, sizeof(plan_vertex->tangent)));
        CHECK(bytes_are_zero(plan_vertex->texture_matrix_id,
                             sizeof(plan_vertex->texture_matrix_id)));
        CHECK(bytes_are_zero(plan_vertex->texcoord, sizeof(plan_vertex->texcoord)));
    }
    for (vertex = 3; vertex < ACGC_APPLE_CANONICAL_PLAN_MAX_VERTEX_COUNT; vertex++) {
        CHECK(bytes_are_zero(&geometry->vertices[vertex],
                             sizeof(geometry->vertices[vertex])));
    }

    CHECK(plan->channels.active_count == 1);
    CHECK(plan->channels.record_valid_mask == 1);
    CHECK(bytes_are_zero(&plan->channels.records[0],
                         sizeof(plan->channels.records[0])));
    CHECK(bytes_are_zero(&plan->channels.records[1],
                         sizeof(plan->channels.records[1])));
    CHECK(plan->texgens.header.active_texgen_count == 0);
    CHECK(plan->texgens.header.known_texgen_count == 0);
    CHECK(plan->texgens.header.texgen_known_mask == 0);
    CHECK(plan->texture.header.known_map_mask == 0);
    CHECK(plan->texture.header.required_map_mask == 0);
    CHECK(bytes_are_zero(plan->texture.records, sizeof(plan->texture.records)));

    CHECK(plan->tev.header.active_stage_count == 1);
    CHECK(tev_stage->color_a == GX_CC_ZERO);
    CHECK(tev_stage->color_b == GX_CC_ZERO);
    CHECK(tev_stage->color_c == GX_CC_ZERO);
    CHECK(tev_stage->color_d == 10);
    CHECK(tev_stage->alpha_a == GX_CA_ZERO);
    CHECK(tev_stage->alpha_b == GX_CA_ZERO);
    CHECK(tev_stage->alpha_c == GX_CA_ZERO);
    CHECK(tev_stage->alpha_d == 5);
    CHECK(tev_stage->color_op == 0);
    CHECK(tev_stage->color_bias == 0);
    CHECK(tev_stage->color_scale == 0);
    CHECK(tev_stage->color_clamp == 1);
    CHECK(tev_stage->color_out == 0);
    CHECK(tev_stage->alpha_op == 0);
    CHECK(tev_stage->alpha_bias == 0);
    CHECK(tev_stage->alpha_scale == 0);
    CHECK(tev_stage->alpha_clamp == 1);
    CHECK(tev_stage->alpha_out == 0);
    CHECK(tev_stage->tex_coord == ACGC_GX_CANONICAL_TEV_TEXCOORD_NULL);
    CHECK(tev_stage->tex_map == ACGC_GX_CANONICAL_TEV_TEXMAP_NULL);
    CHECK(tev_stage->color_chan == 0);
    CHECK(bytes_are_zero(&plan->tev.stages[1],
                         sizeof(plan->tev.stages) - sizeof(plan->tev.stages[0])));
    CHECK(bytes_are_zero(plan->tev.registers, sizeof(plan->tev.registers)));
    CHECK(bytes_are_zero(plan->tev.konst, sizeof(plan->tev.konst)));
    CHECK(bytes_are_zero(plan->tev.swap_tables, sizeof(plan->tev.swap_tables)));

    CHECK(plan->lighting.loaded_mask == 0);
    CHECK(bytes_are_zero(plan->lighting.records, sizeof(plan->lighting.records)));
    CHECK(plan->blend.mode == GX_BM_BLEND);
    CHECK(plan->blend.source_factor == GX_BL_SRCALPHA);
    CHECK(plan->blend.destination_factor == GX_BL_INVSRCALPHA);
    CHECK(plan->blend.logic_op == GX_LO_CLEAR);
    CHECK(plan->alpha.comp0 == GX_ALWAYS);
    CHECK(plan->alpha.ref0 == 0);
    CHECK(plan->alpha.op == GX_AOP_AND);
    CHECK(plan->alpha.comp1 == GX_ALWAYS);
    CHECK(plan->alpha.ref1 == 0);
    CHECK(plan->alpha.color_update_enable == 1);
    CHECK(plan->alpha.alpha_update_enable == 1);
    CHECK(plan->alpha.z_comp_loc_before_tex == 0);
    CHECK(plan->depth.z_compare_enable == 1);
    CHECK(plan->depth.z_compare_func == GX_LEQUAL);
    CHECK(plan->depth.z_update_enable == 1);

    CHECK(plan->raster.viewport_bits[0] == 0);
    CHECK(plan->raster.viewport_bits[1] == 0);
    CHECK(plan->raster.viewport_bits[2] == UINT32_C(0x42800000));
    CHECK(plan->raster.viewport_bits[3] == UINT32_C(0x42800000));
    CHECK(plan->raster.viewport_bits[4] == 0);
    CHECK(plan->raster.viewport_bits[5] == UINT32_C(0x3F800000));
    CHECK(plan->raster.scissor[0] == 0 && plan->raster.scissor[1] == 0);
    CHECK(plan->raster.scissor[2] == 64 && plan->raster.scissor[3] == 64);
    CHECK(plan->raster.scissor_offset[0] == 0 &&
          plan->raster.scissor_offset[1] == 0);
    CHECK(plan->raster.clip_mode == GX_CLIP_ENABLE);
    CHECK(plan->raster.cull_mode == GX_CULL_NONE);
    CHECK(plan->raster.co_planar_enable == 0);
    CHECK(plan->raster.line_width == 0 && plan->raster.line_tex_offsets == 0);
    CHECK(plan->raster.point_size == 0 && plan->raster.point_tex_offsets == 0);
    CHECK(plan->raster.line_texcoord_mask == 0 &&
          plan->raster.point_texcoord_mask == 0);
    CHECK(plan->raster.dither == 0 && plan->raster.dst_alpha_enable == 0 &&
          plan->raster.dst_alpha == 0);
    CHECK(plan->raster.field_mode == 0 && plan->raster.half_aspect_ratio == 0 &&
          plan->raster.field_odd_mask == 0 && plan->raster.field_even_mask == 0);
    CHECK(bytes_are_zero(&plan->fog, sizeof(plan->fog)));
    CHECK(plan->indirect.header.active_indirect_stage_count == 0);
    CHECK(plan->indirect.header.active_order_mask == 0);
    CHECK(plan->indirect.header.matrix_valid_mask == 0);
    CHECK(bytes_are_zero(plan->indirect.orders, sizeof(plan->indirect.orders)));
    CHECK(bytes_are_zero(plan->indirect.matrices, sizeof(plan->indirect.matrices)));
    CHECK(plan->dynamic.header.owner_epoch != 0);
    CHECK(plan->dynamic.header.present_image_mask == 0);
    CHECK(plan->dynamic.header.present_tlut_mask == 0);
    CHECK(plan->dynamic.header.required_image_mask == 0);
    CHECK(plan->dynamic.header.required_tlut_mask == 0);
    CHECK(plan->dynamic.header.present_resource_count == 0);
    CHECK(bytes_are_zero(plan->dynamic.records, sizeof(plan->dynamic.records)));

    CHECK(s_observation.output.state.version == ACGC_METAL_STATE_FIXTURE_VERSION);
    CHECK(s_observation.output.state.transform.matrix[0] ==
          UINT32_C(0x3F800000));
    CHECK(s_observation.output.state.transform.matrix[5] ==
          UINT32_C(0x3F800000));
    CHECK(s_observation.output.state.transform.matrix[10] ==
          UINT32_C(0x3F800000));
    CHECK(s_observation.output.state.transform.matrix[15] ==
          UINT32_C(0x3F800000));
    CHECK(s_observation.output.state.viewport.origin_x == 0);
    CHECK(s_observation.output.state.viewport.origin_y == 0);
    CHECK(s_observation.output.state.viewport.width ==
          UINT32_C(0x42800000));
    CHECK(s_observation.output.state.viewport.height ==
          UINT32_C(0x42800000));
    CHECK(s_observation.output.state.viewport.znear == 0);
    CHECK(s_observation.output.state.viewport.zfar == UINT32_C(0x3F800000));
    CHECK(s_observation.output.state.depth.compare_function ==
          ACGC_METAL_DEPTH_LESS_EQUAL);
    CHECK(s_observation.output.state.depth.write_enabled == 1);
    CHECK(s_observation.output.state.blend.enabled == 1);
    CHECK(s_observation.output.state.blend.source_rgb_factor ==
          ACGC_METAL_BLEND_SOURCE_ALPHA);
    CHECK(s_observation.output.state.blend.destination_rgb_factor ==
          ACGC_METAL_BLEND_ONE_MINUS_SOURCE_ALPHA);
    CHECK(s_observation.output.state.raster.cull_mode == ACGC_METAL_CULL_NONE);
    CHECK(s_observation.output.geometry.version ==
          ACGC_RENDERER_GEOMETRY_VERSION);
    CHECK(s_observation.output.geometry.vertex_count == 3);
    CHECK(s_observation.output.geometry.draw_count == 1);
    CHECK(s_observation.output.geometry.draws[0].primitive ==
          ACGC_RENDERER_PRIMITIVE_TRIANGLES);
    CHECK(s_observation.output.geometry.draws[0].first_vertex == 0);
    CHECK(s_observation.output.geometry.draws[0].vertex_count == 3);
    for (vertex = 0; vertex < 3; vertex++) {
        CHECK(s_observation.output.geometry.vertices[vertex].position_x ==
              expected_positions[vertex][0]);
        CHECK(s_observation.output.geometry.vertices[vertex].position_y ==
              expected_positions[vertex][1]);
        CHECK(s_observation.output.geometry.vertices[vertex].position_z ==
              expected_positions[vertex][2]);
        CHECK(s_observation.output.geometry.vertices[vertex].color_rgba8 ==
              UINT32_C(0xFFFFFFFF));
    }
    CHECK(s_observation.output.texture0_key == 0);
    CHECK(s_observation.output.semantic_version == 0);
    CHECK(s_observation.output.alpha_write_enabled == 1);
    CHECK(s_observation.output.source_kind ==
          ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN);
    return 0;
}

static int test_source_backed_round_trip(void) {
    int callback_count_before;
    AcgcMetalPacketConsumerStatus rejection_status;

    memset(&s_observation, 0, sizeof(s_observation));
    install_gl_stubs();
    pc_gx_init();
    pc_gx_texture_init();
    pc_gx_viewport_state_invalidate();
    configure_known_state();
    CHECK(pc_gx_set_cumulative_snapshot_callback(
        observe_cumulative_envelope,
        &s_observation
    ));

    submit_batch(3);
    CHECK(assert_successful_round_trip(1) == 0);
    s_saved_plan = s_observation.plan;
    s_saved_output = s_observation.output;

    /* The real consumer stages its output and must not partially publish a
     * plan that violates a Geometry predicate. */
    s_invalid_plan = s_observation.plan;
    s_invalid_plan.geometry.present_mask = 0;
    s_rejection_before = s_observation.output;
    s_rejection_output = s_rejection_before;
    rejection_status = acgc_metal_packet_consumer_prepare_canonical_plan(
        &s_invalid_plan,
        &s_rejection_output
    );
    CHECK(rejection_status != ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(memcmp(&s_rejection_output, &s_rejection_before,
                 sizeof(s_rejection_output)) == 0);

    /* Clear/unregistered publication is a successful no-op for the callback
     * seam, and a later registration must still be reusable. */
    CHECK(pc_gx_clear_cumulative_snapshot_callback());
    callback_count_before = s_observation.callback_count;
    submit_batch(3);
    CHECK(s_observation.callback_count == callback_count_before);
    CHECK(!pc_gx_texture_raw_borrow_is_active());
    CHECK(memcmp(&s_observation.plan, &s_saved_plan, sizeof(s_saved_plan)) == 0);
    CHECK(memcmp(&s_observation.output, &s_saved_output,
                 sizeof(s_saved_output)) == 0);

    CHECK(pc_gx_set_cumulative_snapshot_callback(
        observe_cumulative_envelope,
        &s_observation
    ));

    /* A source-backed composition failure must publish zero callbacks and
     * release the borrow before a valid retry. */
    callback_count_before = s_observation.callback_count;
    submit_batch(2);
    CHECK(s_observation.callback_count == callback_count_before);
    CHECK(!pc_gx_texture_raw_borrow_is_active());
    CHECK(memcmp(&s_observation.plan, &s_saved_plan, sizeof(s_saved_plan)) == 0);
    CHECK(memcmp(&s_observation.output, &s_saved_output,
                 sizeof(s_saved_output)) == 0);

    submit_batch(3);
    CHECK(s_observation.callback_count == callback_count_before + 1);
    CHECK(assert_successful_round_trip(callback_count_before + 1) == 0);
    CHECK(memcmp(&s_observation.plan, &s_saved_plan, sizeof(s_saved_plan)) == 0);
    CHECK(memcmp(&s_observation.output, &s_saved_output,
                 sizeof(s_saved_output)) == 0);
    CHECK(!pc_gx_texture_raw_borrow_is_active());

    CHECK(pc_gx_clear_cumulative_snapshot_callback());
    pc_gx_shutdown();
    CHECK(!pc_gx_texture_raw_borrow_is_active());
    return 0;
}

int main(void) {
    CHECK(test_source_backed_round_trip() == 0);
    puts("pc GX canonical plan source-backed round trip: PASS");
    puts("proof boundary: real GX setters and GXBegin/GXEnd captured one direct POS+CLR0 three-vertex envelope, Apple plan parsing and bounded CPU consumer preparation passed, failed composition/consumer rejection published nothing, and borrow/callback storage was reusable; no runtime arbitration, Metal sink, encode/present, pixels, device, assets, or playability claim");
    return 0;
}
