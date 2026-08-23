#include "acgc/apple_canonical_plan.h"

#include "pc_gx_cumulative_snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 0; \
    } \
} while (0)

static uint8_t s_geometry[ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE];
static uint8_t s_transform[ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE];
static uint8_t s_channels[ACGC_GX_CANONICAL_CHANNEL_STATE_SIZE];
static uint8_t s_texgens[ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE];
static uint8_t s_texture[ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE];
static uint8_t s_tev[ACGC_GX_CANONICAL_TEV_STATE_SIZE];
static uint8_t s_lighting[ACGC_GX_CANONICAL_LIGHTING_STATE_SIZE];
static uint8_t s_blend[ACGC_GX_CANONICAL_BLEND_STATE_SIZE];
static uint8_t s_alpha[ACGC_GX_CANONICAL_ALPHA_STATE_SIZE];
static uint8_t s_depth[ACGC_GX_CANONICAL_DEPTH_STATE_SIZE];
static uint8_t s_raster[ACGC_GX_CANONICAL_RASTER_STATE_SIZE];
static uint8_t s_fog[ACGC_GX_CANONICAL_FOG_STATE_SIZE];
static uint8_t s_indirect[ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE];
static uint8_t s_dynamic[ACGC_GX_CANONICAL_DYNAMIC_STATE_SIZE];
static uint8_t s_envelope[PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES];
static uint8_t s_envelope_before[PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES];
static uint8_t s_output_before[sizeof(AcgcAppleCanonicalPlan)];

static AcgcGxCanonicalTransformState s_transform_state;
static AcgcGxCanonicalChannelState s_channel_state;
static AcgcGxCanonicalTexgenState s_texgen_state;
static AcgcGxCanonicalTextureState s_texture_state;
static AcgcGxCanonicalTevState s_tev_state;
static AcgcGxCanonicalLightingState s_lighting_state;
static AcgcGxCanonicalBlendState s_blend_state;
static AcgcGxCanonicalAlphaState s_alpha_state;
static AcgcGxCanonicalDepthState s_depth_state;
static AcgcGxCanonicalRasterState s_raster_state;
static AcgcGxCanonicalFogState s_fog_state;
static AcgcGxCanonicalIndirectState s_indirect_state;
static AcgcGxCanonicalDynamicState s_dynamic_state;
static AcgcAppleCanonicalPlan s_plan;
static AcgcAppleCanonicalPlan s_expected_plan;
static size_t s_geometry_size;
static size_t s_envelope_size;
static int s_indexed_position;
static int s_rich_geometry;

typedef union PlanOverlapStorage {
    AcgcAppleCanonicalPlan plan;
    uint8_t bytes[PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES];
} PlanOverlapStorage;

static PlanOverlapStorage s_overlap;

static uint32_t bits_from_float(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void put_le32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)(value & UINT32_C(0xFF));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    destination[3] = (uint8_t)(value >> 24);
}

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static void geometry_descriptor(
    uint32_t slot,
    uint32_t vcd_type,
    uint32_t vat_count,
    uint32_t vat_type,
    uint32_t vat_fraction,
    uint32_t word_count,
    uint32_t value_offset,
    uint32_t value_bytes,
    uint32_t value_stride,
    uint32_t value_count,
    uint32_t index_offset,
    uint32_t index_bytes,
    uint32_t index_stride,
    uint32_t index_count
) {
    uint8_t* descriptor = s_geometry +
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_OFFSET +
        slot * ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE;

    put_le32(descriptor + 0, vcd_type);
    put_le32(descriptor + 4, vat_count);
    put_le32(descriptor + 8, vat_type);
    put_le32(descriptor + 12, vat_fraction);
    put_le32(descriptor + 16, 1); /* canonical value encoding */
    put_le32(descriptor + 20, word_count);
    put_le32(descriptor + 24, value_offset);
    put_le32(descriptor + 28, value_bytes);
    put_le32(descriptor + 32, value_stride);
    put_le32(descriptor + 36, value_count);
    put_le32(descriptor + 40, index_offset);
    put_le32(descriptor + 44, index_bytes);
    put_le32(descriptor + 48, index_stride);
    put_le32(descriptor + 52, index_count);
    put_le32(descriptor + 56, 0);
    put_le32(descriptor + 60, 0);
}

static void make_geometry(uint32_t primitive, uint32_t vertex_count) {
    const uint32_t stream_offset = ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET;
    const uint32_t value_count = s_indexed_position ? 2 : vertex_count;
    const uint32_t value_bytes = value_count * 3 * sizeof(uint32_t);
    const uint32_t index_offset = s_indexed_position
        ? stream_offset + value_bytes : 0;
    const uint32_t index_bytes = s_indexed_position ? vertex_count * 2 : 0;
    const uint32_t stream_bytes = (value_bytes + index_bytes + 3) & ~UINT32_C(3);
    uint32_t vertex;

    memset(s_geometry, 0, sizeof(s_geometry));
    put_le32(s_geometry + 0, primitive);
    put_le32(s_geometry + 4, vertex_count);
    put_le32(s_geometry + 8, 0); /* VTXFMT 0 */
    put_le32(s_geometry + 12,
             ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT);
    put_le32(s_geometry + 16,
             UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS);
    put_le32(s_geometry + 20, s_indexed_position
             ? UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS : 0);
    put_le32(s_geometry + 24,
             ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_OFFSET);
    put_le32(s_geometry + 28,
             ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_BYTES);
    put_le32(s_geometry + 32,
             ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET);
    put_le32(s_geometry + 36, stream_bytes);
    put_le32(s_geometry + 40, 0);
    put_le32(s_geometry + 44, 0);

    if (s_rich_geometry && !s_indexed_position) {
        uint32_t cursor = stream_offset;
        uint32_t vertex;
        const uint32_t rich_present_mask =
            (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX) |
            (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0MTXIDX) |
            (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS) |
            (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM) |
            (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0) |
            (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR1) |
            (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0);

        put_le32(s_geometry + 16, rich_present_mask);
        geometry_descriptor(
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX,
            ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
            0, 0, 0, 1, cursor, vertex_count * 4, 4, vertex_count,
            0, 0, 0, 0);
        for (vertex = 0; vertex < vertex_count; vertex++) {
            put_le32(s_geometry + cursor + vertex * 4, 0);
        }
        cursor += vertex_count * 4;

        geometry_descriptor(
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0MTXIDX,
            ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
            0, 0, 0, 1, cursor, vertex_count * 4, 4, vertex_count,
            0, 0, 0, 0);
        for (vertex = 0; vertex < vertex_count; vertex++) {
            put_le32(s_geometry + cursor + vertex * 4, 60);
        }
        cursor += vertex_count * 4;

        geometry_descriptor(
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS,
            ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
            ACGC_GX_CANONICAL_GEOMETRY_POS_XYZ,
            ACGC_GX_CANONICAL_GEOMETRY_COMP_F32,
            0, 3, cursor, vertex_count * 12, 12, vertex_count,
            0, 0, 0, 0);
        for (vertex = 0; vertex < vertex_count; vertex++) {
            const size_t offset = cursor + (size_t)vertex * 12;
            put_le32(s_geometry + offset + 0, bits_from_float((float)vertex));
            put_le32(s_geometry + offset + 4, bits_from_float(0.0f));
            put_le32(s_geometry + offset + 8, bits_from_float(0.0f));
        }
        cursor += vertex_count * 12;

        geometry_descriptor(
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM,
            ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
            ACGC_GX_CANONICAL_GEOMETRY_NRM_XYZ,
            ACGC_GX_CANONICAL_GEOMETRY_COMP_F32,
            0, 3, cursor, vertex_count * 12, 12, vertex_count,
            0, 0, 0, 0);
        for (vertex = 0; vertex < vertex_count; vertex++) {
            const size_t offset = cursor + (size_t)vertex * 12;
            put_le32(s_geometry + offset + 0, bits_from_float(1.0f));
            put_le32(s_geometry + offset + 4, bits_from_float(0.0f));
            put_le32(s_geometry + offset + 8, bits_from_float(0.0f));
        }
        cursor += vertex_count * 12;

        geometry_descriptor(
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0,
            ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
            ACGC_GX_CANONICAL_GEOMETRY_CLR_RGBA,
            ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA8,
            0, 1, cursor, vertex_count * 4, 4, vertex_count,
            0, 0, 0, 0);
        for (vertex = 0; vertex < vertex_count; vertex++) {
            put_le32(s_geometry + cursor + vertex * 4, UINT32_C(0x11223344));
        }
        cursor += vertex_count * 4;

        geometry_descriptor(
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR1,
            ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
            ACGC_GX_CANONICAL_GEOMETRY_CLR_RGBA,
            ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA8,
            0, 1, cursor, vertex_count * 4, 4, vertex_count,
            0, 0, 0, 0);
        for (vertex = 0; vertex < vertex_count; vertex++) {
            put_le32(s_geometry + cursor + vertex * 4, UINT32_C(0x55667788));
        }
        cursor += vertex_count * 4;

        geometry_descriptor(
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0,
            ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
            ACGC_GX_CANONICAL_GEOMETRY_TEX_ST,
            ACGC_GX_CANONICAL_GEOMETRY_COMP_F32,
            0, 2, cursor, vertex_count * 8, 8, vertex_count,
            0, 0, 0, 0);
        for (vertex = 0; vertex < vertex_count; vertex++) {
            const size_t offset = cursor + (size_t)vertex * 8;
            put_le32(s_geometry + offset + 0, bits_from_float((float)vertex));
            put_le32(s_geometry + offset + 4, bits_from_float(0.5f));
        }
        cursor += vertex_count * 8;
        put_le32(s_geometry + 36, cursor - stream_offset);
        s_geometry_size = cursor;
        return;
    }

    geometry_descriptor(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS,
        s_indexed_position
            ? ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX16
            : ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
        ACGC_GX_CANONICAL_GEOMETRY_POS_XYZ,
        ACGC_GX_CANONICAL_GEOMETRY_COMP_F32,
        0,
        3,
        stream_offset,
        value_bytes,
        3 * sizeof(uint32_t),
        value_count,
        index_offset,
        index_bytes,
        s_indexed_position ? 2 : 0,
        s_indexed_position ? vertex_count : 0);
    for (vertex = 0; vertex < value_count; vertex++) {
        const size_t offset = stream_offset +
            (size_t)vertex * 3 * sizeof(uint32_t);
        put_le32(s_geometry + offset + 0, bits_from_float((float)vertex));
        put_le32(s_geometry + offset + 4, bits_from_float(0.0f));
        put_le32(s_geometry + offset + 8, bits_from_float(0.0f));
    }
    if (s_indexed_position) {
        for (vertex = 0; vertex < vertex_count; vertex++) {
            const uint16_t index = vertex == 2 ? 0 : (uint16_t)vertex;
            const size_t offset = index_offset + (size_t)vertex * 2;
            s_geometry[offset] = (uint8_t)(index & UINT16_C(0xFF));
            s_geometry[offset + 1] = (uint8_t)(index >> 8);
        }
    }
    s_geometry_size = stream_offset + stream_bytes;
}

static void fill_transform(void) {
    memset(&s_transform_state, 0, sizeof(s_transform_state));
    s_transform_state.known_mask =
        ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK |
        ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(0);
    if (s_rich_geometry) {
        s_transform_state.known_mask |=
            ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(0);
    }
    s_transform_state.current_position_id =
        ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_FIRST;
    s_transform_state.position[0][0] = bits_from_float(1.0f);
}

static void fill_channels(void) {
    memset(&s_channel_state, 0, sizeof(s_channel_state));
}

static void fill_texgens(void) {
    AcgcGxCanonicalTexgenMatrixRecord* ordinary;
    AcgcGxCanonicalTexgenMatrixRecord* post;
    uint32_t index;

    memset(&s_texgen_state, 0, sizeof(s_texgen_state));
    s_texgen_state.header.active_texgen_count = 1;
    s_texgen_state.header.texgen_capacity =
        ACGC_GX_CANONICAL_TEXGEN_CAPACITY;
    s_texgen_state.header.known_texgen_count = 1;
    s_texgen_state.header.ordinary_matrix_count =
        UINT32_C(1);
    s_texgen_state.header.ordinary_matrix_capacity =
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_CAPACITY;
    s_texgen_state.header.post_matrix_count =
        UINT32_C(1);
    s_texgen_state.header.post_matrix_capacity =
        ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_CAPACITY;
    s_texgen_state.header.su_count = 0;
    s_texgen_state.header.su_capacity =
        ACGC_GX_CANONICAL_TEXGEN_SU_CAPACITY;
    s_texgen_state.header.texgen_known_mask = 1;
    s_texgen_state.header.ordinary_matrix_known_mask = UINT32_C(1) << 10;
    s_texgen_state.header.post_matrix_known_mask = UINT32_C(1) << 20;
    s_texgen_state.header.component_known_summary = 0;

    /* Unknown slots still carry their canonical logical IDs. */
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT;
         index++) {
        s_texgen_state.ordinary_matrix[index].logical_id =
            ACGC_GX_CANONICAL_TEXGEN_ORDINARY_LOGICAL_ID(index);
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT;
         index++) {
        s_texgen_state.post_matrix[index].logical_id =
            ACGC_GX_CANONICAL_TEXGEN_POST_LOGICAL_ID(index);
    }
    s_texgen_state.texgen[0].function =
        ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX3X4;
    s_texgen_state.texgen[0].source =
        ACGC_GX_CANONICAL_TEXGEN_SOURCE_POS;
    s_texgen_state.texgen[0].ordinary_matrix_id = 60;
    s_texgen_state.texgen[0].post_matrix_id =
        ACGC_GX_CANONICAL_GEOMETRY_POST_IDENTITY;
    s_texgen_state.texgen[0].component_known =
        ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL;

    ordinary = &s_texgen_state.ordinary_matrix[10];
    ordinary->logical_id = 60;
    ordinary->last_load_type =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4;
    ordinary->last_written_word_count =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4;
    ordinary->known_word_mask =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4;
    post = &s_texgen_state.post_matrix[20];
    post->logical_id = ACGC_GX_CANONICAL_GEOMETRY_POST_IDENTITY;
    post->last_load_type = ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4;
    post->last_written_word_count =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4;
    post->known_word_mask =
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4;
}

static void fill_texture(void) {
    AcgcGxCanonicalTextureRecord* record;

    memset(&s_texture_state, 0, sizeof(s_texture_state));
    s_texture_state.header.known_map_mask = 1;
    s_texture_state.header.known_map_count = 1;
    s_texture_state.header.required_map_mask = 1;
    s_texture_state.header.record_byte_offset =
        ACGC_GX_CANONICAL_TEXTURE_HEADER_BYTE_SIZE;
    s_texture_state.header.record_count =
        ACGC_GX_CANONICAL_TEXTURE_STATE_COUNT;
    s_texture_state.header.record_capacity =
        ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY;
    s_texture_state.header.record_word_count =
        ACGC_GX_CANONICAL_TEXTURE_RECORD_WORD_COUNT;
    s_texture_state.header.resource_id_scheme =
        ACGC_GX_CANONICAL_TEXTURE_RESOURCE_ID_SCHEME;
    record = &s_texture_state.records[0];
    record->flags = ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED;
    record->image_resource_id = 1;
    record->image_owner_epoch = 7;
    record->image_generation_lo = 9;
    record->image_generation_hi = 10;
    record->width = 8;
    record->height = 4;
    record->image_format = ACGC_GX_CANONICAL_TEXTURE_FORMAT_I8;
    record->wrap_s = ACGC_GX_CANONICAL_TEXTURE_WRAP_CLAMP;
    record->wrap_t = ACGC_GX_CANONICAL_TEXTURE_WRAP_CLAMP;
    record->min_filter = 0;
    record->mag_filter = 0;
    record->min_lod_q4 = 0;
    record->max_lod_q4 = 0;
    record->lod_bias_q5 = 0;
    record->bias_clamp = 0;
    record->edge_lod = 0;
    record->max_anisotropy = 0;
    record->mip_level_count = 1;
    record->image_byte_size = 32;
    record->image_byte_order = ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_GX_BE;
    record->image_source_kind =
        ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST;
    record->tlut_name = ACGC_GX_CANONICAL_TEXTURE_TLUT_NAME_NONE;
}

static void fill_tev(void) {
    memset(&s_tev_state, 0, sizeof(s_tev_state));
    s_tev_state.header.version = ACGC_GX_CANONICAL_TEV_STATE_VERSION;
    s_tev_state.header.section_id = ACGC_GX_CANONICAL_TEV_SECTION_ID;
    s_tev_state.header.section_mask = ACGC_GX_CANONICAL_TEV_SECTION_MASK;
    s_tev_state.header.byte_size = ACGC_GX_CANONICAL_TEV_STATE_SIZE;
    s_tev_state.header.active_stage_count = 1;
    s_tev_state.header.stage_capacity = ACGC_GX_CANONICAL_TEV_STAGE_CAPACITY;
    s_tev_state.header.component_valid_mask =
        ACGC_GX_CANONICAL_TEV_COMPONENT_VALID_MASK;
    s_tev_state.header.stage_offset = ACGC_GX_CANONICAL_TEV_STAGE_OFFSET;
    s_tev_state.header.stage_record_size =
        ACGC_GX_CANONICAL_TEV_STAGE_RECORD_SIZE;
    s_tev_state.header.register_offset =
        ACGC_GX_CANONICAL_TEV_REGISTER_OFFSET;
    s_tev_state.header.register_record_size =
        ACGC_GX_CANONICAL_TEV_REGISTER_RECORD_SIZE;
    s_tev_state.header.konst_offset = ACGC_GX_CANONICAL_TEV_KONST_OFFSET;
    s_tev_state.header.konst_record_size =
        ACGC_GX_CANONICAL_TEV_KONST_RECORD_SIZE;
    s_tev_state.header.swap_table_offset =
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_OFFSET;
    s_tev_state.header.swap_table_record_size =
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_RECORD_SIZE;
    s_tev_state.stages[0].tex_coord = 0;
    s_tev_state.stages[0].tex_map = 0;
    s_tev_state.stages[0].color_chan =
        ACGC_GX_CANONICAL_TEV_CHANNEL_NULL;
}

static void fill_lighting(void) {
    memset(&s_lighting_state, 0, sizeof(s_lighting_state));
}

static void fill_raster(void) {
    memset(&s_raster_state, 0, sizeof(s_raster_state));
    s_raster_state.viewport_bits[0] = UINT32_C(0x80000000);
    s_raster_state.viewport_bits[1] = UINT32_C(0x3F800000);
    s_raster_state.viewport_bits[2] = UINT32_C(0x40490FDB);
    s_raster_state.viewport_bits[3] = UINT32_C(0x00000001);
    s_raster_state.viewport_bits[4] = UINT32_C(0xBF800000);
    s_raster_state.viewport_bits[5] = UINT32_C(0x7F7FFFFF);
    s_raster_state.scissor[2] = 640;
    s_raster_state.scissor[3] = 480;
    s_raster_state.scissor_offset[0] =
        ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MIN;
    s_raster_state.scissor_offset[1] =
        ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MAX;
    s_raster_state.clip_mode = ACGC_GX_CANONICAL_RASTER_CLIP_MODE_DISABLE;
    s_raster_state.cull_mode = ACGC_GX_CANONICAL_RASTER_CULL_MODE_ALL;
    s_raster_state.co_planar_enable = 1;
    s_raster_state.line_width = ACGC_GX_CANONICAL_RASTER_SIZE_MAX;
    s_raster_state.line_tex_offsets = ACGC_GX_CANONICAL_RASTER_TEX_OFFSET_MAX;
    s_raster_state.point_tex_offsets = ACGC_GX_CANONICAL_RASTER_TEX_OFFSET_MIN;
    s_raster_state.line_texcoord_mask = UINT32_C(0xA5);
    s_raster_state.point_texcoord_mask = UINT32_C(0x5A);
    s_raster_state.dither = 1;
    s_raster_state.dst_alpha_enable = 1;
    s_raster_state.dst_alpha = ACGC_GX_CANONICAL_RASTER_DST_ALPHA_MAX;
    s_raster_state.field_mode = 1;
    s_raster_state.field_odd_mask = 1;
}

static void fill_indirect(void) {
    memset(&s_indirect_state, 0, sizeof(s_indirect_state));
    s_indirect_state.header.version =
        ACGC_GX_CANONICAL_INDIRECT_STATE_VERSION;
    s_indirect_state.header.section_id =
        ACGC_GX_CANONICAL_INDIRECT_SECTION_ID;
    s_indirect_state.header.section_mask =
        ACGC_GX_CANONICAL_INDIRECT_SECTION_MASK;
    s_indirect_state.header.byte_size =
        ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE;
    s_indirect_state.header.active_indirect_stage_count = 0;
    s_indirect_state.header.order_capacity =
        ACGC_GX_CANONICAL_INDIRECT_ORDER_CAPACITY;
    s_indirect_state.header.order_record_size =
        ACGC_GX_CANONICAL_INDIRECT_ORDER_RECORD_SIZE;
    s_indirect_state.header.order_offset =
        ACGC_GX_CANONICAL_INDIRECT_ORDER_OFFSET;
    s_indirect_state.header.active_order_mask = 0;
    s_indirect_state.header.matrix_capacity =
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_CAPACITY;
    s_indirect_state.header.matrix_record_size =
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_RECORD_SIZE;
    s_indirect_state.header.matrix_offset =
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_OFFSET;
    s_indirect_state.header.matrix_valid_mask = 0;
}

static void fill_dynamic(void) {
    AcgcGxCanonicalDynamicRecord* record;

    memset(&s_dynamic_state, 0, sizeof(s_dynamic_state));
    s_dynamic_state.header.owner_epoch = 7;
    s_dynamic_state.header.present_image_mask = 1;
    s_dynamic_state.header.required_image_mask = 1;
    s_dynamic_state.header.present_resource_count = 1;
    s_dynamic_state.header.record_byte_offset =
        ACGC_GX_CANONICAL_DYNAMIC_HEADER_BYTE_SIZE;
    s_dynamic_state.header.record_count =
        ACGC_GX_CANONICAL_DYNAMIC_STATE_COUNT;
    s_dynamic_state.header.record_capacity =
        ACGC_GX_CANONICAL_DYNAMIC_STATE_CAPACITY;
    s_dynamic_state.header.record_word_count =
        ACGC_GX_CANONICAL_DYNAMIC_RECORD_WORD_COUNT;
    s_dynamic_state.header.resource_id_scheme =
        ACGC_GX_CANONICAL_DYNAMIC_RESOURCE_ID_SCHEME;
    record = &s_dynamic_state.records[0];
    record->resource_id = 1;
    record->kind = ACGC_GX_CANONICAL_DYNAMIC_KIND_IMAGE;
    record->owner_epoch = 7;
    record->generation_lo = 9;
    record->generation_hi = 10;
    record->owner_slot = 0;
    record->byte_flags = ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
        ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED;
    record->byte_size = 32;
    record->byte_order = ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_GX_BE;
    record->alignment = ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES;
    record->source_kind = ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_RAW_GUEST;
    record->format = ACGC_GX_CANONICAL_DYNAMIC_FORMAT_I8;
}

static int encode_sections(uint32_t primitive, uint32_t vertex_count) {
    PCGXCumulativeSnapshotSection sections[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT];
    const uint8_t* bytes[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT] = {
        s_geometry, s_transform, s_channels, s_texgens, s_texture, s_tev,
        s_lighting, s_blend, s_alpha, s_depth, s_raster, s_fog, s_indirect,
        s_dynamic
    };
    const size_t sizes[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT] = {
        0,
        sizeof(s_transform), sizeof(s_channels), sizeof(s_texgens),
        sizeof(s_texture), sizeof(s_tev), sizeof(s_lighting), sizeof(s_blend),
        sizeof(s_alpha), sizeof(s_depth), sizeof(s_raster), sizeof(s_fog),
        sizeof(s_indirect), sizeof(s_dynamic)
    };
    const uint32_t counts[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT] = {
        1, 1, 2, 1, 8, 1, 8, 1, 1, 1, 1, 1, 1, 24
    };
    const uint32_t capacities[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT] = {
        1, 1, 2, 1, 8, 16, 8, 1, 1, 1, 1, 1, 1, 24
    };
    const uint32_t masks[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT] = {
        ACGC_GX_CANONICAL_SECTION_MASK_GEOMETRY,
        ACGC_GX_CANONICAL_SECTION_MASK_TRANSFORMS,
        ACGC_GX_CANONICAL_SECTION_MASK_CHANNELS,
        ACGC_GX_CANONICAL_SECTION_MASK_TEXGENS,
        ACGC_GX_CANONICAL_SECTION_MASK_TEXTURES,
        ACGC_GX_CANONICAL_SECTION_MASK_TEV,
        ACGC_GX_CANONICAL_SECTION_MASK_LIGHTING,
        ACGC_GX_CANONICAL_SECTION_MASK_BLEND,
        ACGC_GX_CANONICAL_SECTION_MASK_ALPHA,
        ACGC_GX_CANONICAL_SECTION_MASK_DEPTH,
        ACGC_GX_CANONICAL_SECTION_MASK_RASTER,
        ACGC_GX_CANONICAL_SECTION_MASK_FOG,
        ACGC_GX_CANONICAL_SECTION_MASK_INDIRECT,
        ACGC_GX_CANONICAL_SECTION_MASK_DYNAMIC
    };
    uint32_t index;
    size_t actual_size = 0;

    make_geometry(primitive, vertex_count);
    fill_transform();
    fill_channels();
    fill_texgens();
    CHECK(acgc_gx_canonical_texgen_state_validate(&s_texgen_state));
    fill_texture();
    fill_tev();
    fill_lighting();
    memset(&s_blend_state, 0, sizeof(s_blend_state));
    memset(&s_alpha_state, 0, sizeof(s_alpha_state));
    memset(&s_depth_state, 0, sizeof(s_depth_state));
    fill_raster();
    memset(&s_fog_state, 0, sizeof(s_fog_state));
    fill_indirect();
    fill_dynamic();

    CHECK(acgc_gx_canonical_transform_state_encode(
        &s_transform_state, s_transform, sizeof(s_transform)));
    CHECK(acgc_gx_canonical_channel_state_encode(
        &s_channel_state, s_channels, sizeof(s_channels)));
    CHECK(acgc_gx_canonical_texgen_state_encode(
        &s_texgen_state, s_texgens, sizeof(s_texgens)));
    CHECK(acgc_gx_canonical_texture_state_encode(
        &s_texture_state, s_texture, sizeof(s_texture)));
    CHECK(acgc_gx_canonical_tev_state_encode(
        &s_tev_state, s_tev, sizeof(s_tev)));
    CHECK(acgc_gx_canonical_lighting_state_encode(
        &s_lighting_state, s_lighting, sizeof(s_lighting)));
    CHECK(acgc_gx_canonical_blend_state_encode(
        &s_blend_state, s_blend, sizeof(s_blend)));
    CHECK(acgc_gx_canonical_alpha_state_encode(
        &s_alpha_state, s_alpha, sizeof(s_alpha)));
    CHECK(acgc_gx_canonical_depth_state_encode(
        &s_depth_state, s_depth, sizeof(s_depth)));
    CHECK(acgc_gx_canonical_raster_state_encode(
        &s_raster_state, s_raster, sizeof(s_raster)));
    CHECK(acgc_gx_canonical_fog_state_encode(
        &s_fog_state, s_fog, sizeof(s_fog)));
    CHECK(acgc_gx_canonical_indirect_state_encode(
        &s_indirect_state, s_indirect, sizeof(s_indirect)));
    CHECK(acgc_gx_canonical_dynamic_state_encode(
        &s_dynamic_state, s_dynamic, sizeof(s_dynamic)));

    memset(sections, 0, sizeof(sections));
    for (index = 0; index < PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT; index++) {
        sections[index].section_id = index + 1;
        sections[index].section_version =
            ACGC_GX_CANONICAL_SECTION_VERSION;
        sections[index].byte_size = index == 0 ? s_geometry_size : sizes[index];
        sections[index].count = counts[index];
        sections[index].capacity = capacities[index];
        sections[index].valid_mask = masks[index];
        sections[index].bytes.data = bytes[index];
        sections[index].bytes.size = sections[index].byte_size;
    }
    memset(s_envelope, 0xCD, sizeof(s_envelope));
    CHECK(pc_gx_cumulative_snapshot_assemble(
        sections,
        s_envelope,
        sizeof(s_envelope),
        &actual_size));
    s_envelope_size = actual_size;
    return 1;
}

static size_t section_offset(uint32_t section_index) {
    return read_le32(
        s_envelope + ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET +
            section_index * ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE +
            8);
}

static int build_success_and_verify(void) {
    AcgcAppleCanonicalPlanStatus status;

    memset(&s_plan, 0xA5, sizeof(s_plan));
    status = acgc_apple_canonical_plan_build(
        s_envelope, s_envelope_size, &s_plan);
    CHECK(status == ACGC_APPLE_CANONICAL_PLAN_OK);
    CHECK(s_plan.geometry.vertex_count == 3);
    CHECK(s_plan.geometry.primitive ==
          ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES);
    CHECK(s_plan.geometry.vertices[0].position[0] == bits_from_float(0.0f));
    CHECK(s_plan.geometry.vertices[2].position[0] == bits_from_float(2.0f));
    CHECK(s_plan.geometry.vertices[0].texture_matrix_id[0] == 60);
    CHECK(s_plan.geometry.vertices[0].position_matrix_id == 0);
    CHECK(memcmp(&s_plan.transform, &s_transform_state,
                 sizeof(s_plan.transform)) == 0);
    CHECK(memcmp(&s_plan.channels, &s_channel_state,
                 sizeof(s_plan.channels)) == 0);
    CHECK(memcmp(&s_plan.texgens, &s_texgen_state,
                 sizeof(s_plan.texgens)) == 0);
    CHECK(memcmp(&s_plan.texture, &s_texture_state,
                 sizeof(s_plan.texture)) == 0);
    CHECK(memcmp(&s_plan.tev, &s_tev_state, sizeof(s_plan.tev)) == 0);
    CHECK(memcmp(&s_plan.lighting, &s_lighting_state,
                 sizeof(s_plan.lighting)) == 0);
    CHECK(memcmp(&s_plan.blend, &s_blend_state, sizeof(s_plan.blend)) == 0);
    CHECK(memcmp(&s_plan.alpha, &s_alpha_state, sizeof(s_plan.alpha)) == 0);
    CHECK(memcmp(&s_plan.depth, &s_depth_state, sizeof(s_plan.depth)) == 0);
    CHECK(memcmp(&s_plan.raster, &s_raster_state,
                 sizeof(s_plan.raster)) == 0);
    CHECK(memcmp(&s_plan.fog, &s_fog_state, sizeof(s_plan.fog)) == 0);
    CHECK(memcmp(&s_plan.indirect, &s_indirect_state,
                 sizeof(s_plan.indirect)) == 0);
    CHECK(memcmp(&s_plan.dynamic, &s_dynamic_state,
                 sizeof(s_plan.dynamic)) == 0);
    s_expected_plan = s_plan;
    memset(s_envelope, 0, s_envelope_size);
    CHECK(memcmp(&s_plan, &s_expected_plan, sizeof(s_plan)) == 0);

    {
        uint8_t* owned_envelope = (uint8_t*)malloc(s_envelope_size);
        AcgcAppleCanonicalPlan retained_plan;

        CHECK(owned_envelope != NULL);
        CHECK(encode_sections(
            ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
        memcpy(owned_envelope, s_envelope, s_envelope_size);
        CHECK(acgc_apple_canonical_plan_build(
                  owned_envelope, s_envelope_size, &retained_plan) ==
              ACGC_APPLE_CANONICAL_PLAN_OK);
        free(owned_envelope);
        CHECK(memcmp(
                  &retained_plan, &s_expected_plan, sizeof(retained_plan)) == 0);
    }
    return 1;
}

static int expect_failure(
    AcgcAppleCanonicalPlanStatus expected_status
) {
    AcgcAppleCanonicalPlanStatus status;

    memcpy(s_envelope_before, s_envelope, s_envelope_size);
    memset(&s_plan, 0xA7, sizeof(s_plan));
    memcpy(s_output_before, &s_plan, sizeof(s_plan));
    status = acgc_apple_canonical_plan_build(
        s_envelope, s_envelope_size, &s_plan);
    CHECK(status == expected_status);
    CHECK(memcmp(s_envelope, s_envelope_before, s_envelope_size) == 0);
    CHECK(memcmp(&s_plan, s_output_before, sizeof(s_plan)) == 0);
    return 1;
}

static int test_argument_failures(void) {
    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));

    memset(&s_plan, 0xA7, sizeof(s_plan));
    memcpy(s_output_before, &s_plan, sizeof(s_plan));
    CHECK(acgc_apple_canonical_plan_build(
              NULL, s_envelope_size, &s_plan) ==
          ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT);
    CHECK(memcmp(&s_plan, s_output_before, sizeof(s_plan)) == 0);

    memcpy(s_envelope_before, s_envelope, s_envelope_size);
    memset(&s_plan, 0xA7, sizeof(s_plan));
    memcpy(s_output_before, &s_plan, sizeof(s_plan));
    CHECK(acgc_apple_canonical_plan_build(
              s_envelope, 0, &s_plan) ==
          ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT);
    CHECK(memcmp(s_envelope, s_envelope_before, s_envelope_size) == 0);
    CHECK(memcmp(&s_plan, s_output_before, sizeof(s_plan)) == 0);

    memcpy(s_envelope_before, s_envelope, s_envelope_size);
    CHECK(acgc_apple_canonical_plan_build(
              s_envelope, s_envelope_size, NULL) ==
          ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT);
    CHECK(memcmp(s_envelope, s_envelope_before, s_envelope_size) == 0);
    return 1;
}

static int test_rich_geometry(void) {
    const uint32_t expected_present_mask =
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0MTXIDX) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR1) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0);
    AcgcAppleCanonicalPlanStatus status;

    s_rich_geometry = 1;
    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
    memset(&s_plan, 0, sizeof(s_plan));
    status = acgc_apple_canonical_plan_build(
        s_envelope, s_envelope_size, &s_plan);
    CHECK(status == ACGC_APPLE_CANONICAL_PLAN_OK);
    CHECK(s_plan.geometry.present_mask == expected_present_mask);
    CHECK((s_plan.geometry.component_mask &
           ACGC_APPLE_CANONICAL_PLAN_COMPONENT_POSITION) != 0);
    CHECK((s_plan.geometry.component_mask &
           ACGC_APPLE_CANONICAL_PLAN_COMPONENT_NORMAL) != 0);
    CHECK((s_plan.geometry.component_mask &
           ACGC_APPLE_CANONICAL_PLAN_COMPONENT_COLOR0) != 0);
    CHECK((s_plan.geometry.component_mask &
           ACGC_APPLE_CANONICAL_PLAN_COMPONENT_COLOR1) != 0);
    CHECK((s_plan.geometry.component_mask &
           ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TEXCOORD0) != 0);
    CHECK((s_plan.geometry.component_mask &
           (ACGC_APPLE_CANONICAL_PLAN_COMPONENT_BINORMAL |
            ACGC_APPLE_CANONICAL_PLAN_COMPONENT_TANGENT)) == 0);
    CHECK(s_plan.geometry.vertices[1].position_matrix_id == 0);
    CHECK(s_plan.geometry.vertices[1].texture_matrix_id[0] == 60);
    CHECK(s_plan.geometry.vertices[1].normal[0] == bits_from_float(1.0f));
    CHECK(s_plan.geometry.vertices[1].color_rgba8[0] == UINT32_C(0x44332211));
    CHECK(s_plan.geometry.vertices[1].color_rgba8[1] == UINT32_C(0x88776655));
    CHECK(s_plan.geometry.vertices[1].texcoord[0][0] == bits_from_float(1.0f));
    CHECK(s_plan.geometry.vertices[1].texcoord[0][1] == bits_from_float(0.5f));
    s_rich_geometry = 0;
    return 1;
}

static int test_failure_boundaries(void) {
    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));

    put_le32(s_envelope + section_offset(7), 99);
    CHECK(expect_failure(ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC));

    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
    put_le32(s_envelope + section_offset(13) + 64 + 12, 99);
    CHECK(expect_failure(ACGC_APPLE_CANONICAL_PLAN_RESOURCE_METADATA));

    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
    put_le32(s_envelope + section_offset(0), 99);
    CHECK(expect_failure(ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT));

    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
    memcpy(s_envelope_before, s_envelope, s_envelope_size);
    memset(&s_plan, 0xA7, sizeof(s_plan));
    memcpy(s_output_before, &s_plan, sizeof(s_plan));
    CHECK(acgc_apple_canonical_plan_build(
              s_envelope, s_envelope_size - 4, &s_plan) ==
          ACGC_APPLE_CANONICAL_PLAN_STRUCTURAL);
    CHECK(memcmp(s_envelope, s_envelope_before, s_envelope_size) == 0);
    CHECK(memcmp(&s_plan, s_output_before, sizeof(s_plan)) == 0);

    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
    put_le32(s_envelope + section_offset(3) + 0, 2);
    put_le32(s_envelope + section_offset(3) + 8, 2);
    put_le32(s_envelope + section_offset(3) + 36, 3);
    put_le32(s_envelope + section_offset(3) + 96 + 0,
             ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP0);
    put_le32(s_envelope + section_offset(3) + 96 + 4,
             ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD0);
    put_le32(s_envelope + section_offset(3) + 96 + 8, 60);
    put_le32(s_envelope + section_offset(3) + 96 + 12, 0);
    put_le32(s_envelope + section_offset(3) + 96 + 16, 125);
    put_le32(s_envelope + section_offset(3) + 96 + 20,
             ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL);
    put_le32(s_envelope + section_offset(3) + 96 + 24, 0);
    put_le32(s_envelope + section_offset(3) + 96 + 28, 0);
    CHECK(expect_failure(ACGC_APPLE_CANONICAL_PLAN_UNSUPPORTED_BUMP));
    return 1;
}

static int test_dependency_failures(void) {
    size_t transform_offset;
    size_t channels_offset;
    size_t indirect_offset;

    s_rich_geometry = 1;
    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
    transform_offset = section_offset(1);
    put_le32(
        s_envelope + transform_offset +
            ACGC_GX_CANONICAL_TRANSFORM_KNOWN_MASK_OFFSET,
        ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK |
            ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(0));
    CHECK(expect_failure(ACGC_APPLE_CANONICAL_PLAN_DEPENDENCY));
    s_rich_geometry = 0;

    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
    channels_offset = section_offset(2);
    put_le32(s_envelope + channels_offset + 0, 1);
    put_le32(s_envelope + channels_offset + 4, 1);
    put_le32(s_envelope + channels_offset + 16, 1);
    put_le32(s_envelope + channels_offset + 20, 1);
    CHECK(expect_failure(ACGC_APPLE_CANONICAL_PLAN_DEPENDENCY));

    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
    indirect_offset = section_offset(12);
    put_le32(s_envelope + indirect_offset + 16, 1);
    put_le32(s_envelope + indirect_offset + 32, 1);
    put_le32(s_envelope + indirect_offset + 56 + 0, 0);
    put_le32(s_envelope + indirect_offset + 56 + 4, 1);
    CHECK(expect_failure(ACGC_APPLE_CANONICAL_PLAN_DEPENDENCY));
    return 1;
}

static int test_overlap_and_bound_geometry(void) {
    AcgcAppleCanonicalPlanStatus status;

    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
    memcpy(s_overlap.bytes, s_envelope, s_envelope_size);
    status = acgc_apple_canonical_plan_build(
        s_overlap.bytes, s_envelope_size,
        (AcgcAppleCanonicalPlan*)(void*)s_overlap.bytes);
    CHECK(status == ACGC_APPLE_CANONICAL_PLAN_INPUT_OUTPUT_OVERLAP);

    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
    put_le32(
        s_envelope + section_offset(0) + 20,
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS_MTX_ARRAY);
    CHECK(expect_failure(ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT));

    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS, 128));
    memset(&s_plan, 0, sizeof(s_plan));
    status = acgc_apple_canonical_plan_build(
        s_envelope, s_envelope_size, &s_plan);
    CHECK(status == ACGC_APPLE_CANONICAL_PLAN_OK);
    CHECK(s_plan.geometry.vertex_count == 128);
    CHECK(s_plan.geometry.vertices[127].position[0] ==
          bits_from_float(127.0f));

    s_indexed_position = 1;
    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
    memset(&s_plan, 0, sizeof(s_plan));
    status = acgc_apple_canonical_plan_build(
        s_envelope, s_envelope_size, &s_plan);
    CHECK(status == ACGC_APPLE_CANONICAL_PLAN_OK);
    CHECK(s_plan.geometry.vertex_count == 3);
    CHECK(s_plan.geometry.vertices[0].position[0] == bits_from_float(0.0f));
    CHECK(s_plan.geometry.vertices[1].position[0] == bits_from_float(1.0f));
    CHECK(s_plan.geometry.vertices[2].position[0] == bits_from_float(0.0f));
    s_indexed_position = 0;

    put_le32(s_envelope + section_offset(0) + 4, 129);
    CHECK(expect_failure(ACGC_APPLE_CANONICAL_PLAN_GEOMETRY_LIMIT));
    return 1;
}

static int run_tests(void) {
    CHECK(encode_sections(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES, 3));
    CHECK(build_success_and_verify());
    CHECK(test_argument_failures());
    CHECK(test_rich_geometry());
    CHECK(test_failure_boundaries());
    CHECK(test_dependency_failures());
    CHECK(test_overlap_and_bound_geometry());
    printf("Apple canonical plan tests: PASS\n");
    return 1;
}

int main(void) {
    /* Helpers use 0 for failure; convert that convention to process failure. */
    return run_tests() ? 0 : 1;
}
