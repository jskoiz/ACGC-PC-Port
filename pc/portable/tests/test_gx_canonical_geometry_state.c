#include "acgc/gx_canonical_geometry_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", \
                    __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static uint8_t section_bytes[
    ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE];

static void put_le16(uint8_t* bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value & UINT16_C(0xFF));
    bytes[1] = (uint8_t)((value >> 8) & UINT16_C(0xFF));
}

static void put_le32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & UINT32_C(0xFF));
    bytes[1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    bytes[2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    bytes[3] = (uint8_t)((value >> 24) & UINT32_C(0xFF));
}

static uint32_t bits_from_float(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static size_t descriptor_offset(uint32_t slot, uint32_t field_offset) {
    return (size_t)ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_OFFSET +
        (size_t)slot * ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE +
        field_offset;
}

static void put_descriptor_word(
    uint32_t slot,
    uint32_t field_offset,
    uint32_t value
) {
    put_le32(section_bytes + descriptor_offset(slot, field_offset), value);
}

static void set_header(
    uint32_t primitive,
    uint32_t vertex_count,
    uint32_t stream_bytes,
    uint32_t present_mask,
    uint32_t indexed_mask
) {
    memset(section_bytes, 0, sizeof(section_bytes));
    put_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_SECTION_ID_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_SECTION_ID);
    put_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_VERSION_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_STATE_VERSION);
    put_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_MASK_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_SECTION_MASK);
    put_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_BYTE_SIZE_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_HEADER_SIZE);
    put_le32(
        section_bytes +
            ACGC_GX_CANONICAL_GEOMETRY_HEADER_DESCRIPTOR_SIZE_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE);
    put_le32(
        section_bytes +
            ACGC_GX_CANONICAL_GEOMETRY_HEADER_DESCRIPTOR_COUNT_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT);
    put_le32(
        section_bytes +
            ACGC_GX_CANONICAL_GEOMETRY_HEADER_STREAM_OFFSET_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET);
    put_le32(
        section_bytes +
            ACGC_GX_CANONICAL_GEOMETRY_HEADER_STREAM_BYTES_OFFSET,
        stream_bytes);
    put_le32(
        section_bytes +
            ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRIMITIVE_OFFSET,
        primitive);
    put_le32(
        section_bytes +
            ACGC_GX_CANONICAL_GEOMETRY_HEADER_VERTEX_COUNT_OFFSET,
        vertex_count);
    put_le32(
        section_bytes +
            ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRESENT_MASK_OFFSET,
        present_mask);
    put_le32(
        section_bytes +
            ACGC_GX_CANONICAL_GEOMETRY_HEADER_INDEXED_MASK_OFFSET,
        indexed_mask);
}

static void set_descriptor(
    uint32_t slot,
    uint32_t vcd_type,
    uint32_t vat_count,
    uint32_t vat_type,
    uint32_t vat_fraction,
    uint32_t word_count,
    uint32_t value_offset,
    uint32_t value_count,
    uint32_t index_offset,
    uint32_t index_stride,
    uint32_t index_count
) {
    const uint32_t value_stride = word_count * 4;
    const uint32_t index_bytes = index_stride * index_count;

    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VCD_TYPE_OFFSET,
        vcd_type);
    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_COUNT_OFFSET,
        vat_count);
    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_TYPE_OFFSET,
        vat_type);
    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_FRACTION_OFFSET,
        vat_fraction);
    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_ENCODING_OFFSET,
        1);
    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_CANONICAL_WORD_COUNT_OFFSET,
        word_count);
    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_OFFSET,
        value_offset);
    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_BYTES_OFFSET,
        value_count * value_stride);
    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_STRIDE_OFFSET,
        value_stride);
    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_COUNT_OFFSET,
        value_count);
    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_OFFSET,
        index_offset);
    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_BYTES_OFFSET,
        index_bytes);
    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_STRIDE_OFFSET,
        index_stride);
    put_descriptor_word(
        slot,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_COUNT_OFFSET,
        index_count);
}

/* This variant avoids relying on a record stride expression at call sites. */
static void put_record_word(
    uint32_t offset,
    uint32_t stride,
    uint32_t record,
    uint32_t word,
    uint32_t value
) {
    put_le32(
        section_bytes + offset + record * stride + word * 4,
        value);
}

static void make_direct_triangle(void) {
    const uint32_t present_mask =
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0);
    const uint32_t position_offset =
        ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET;
    const uint32_t color_offset = position_offset + 3 * 3 * 4;
    const uint32_t texcoord_offset = color_offset + 3 * 4;

    set_header(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES,
        3,
        3 * 3 * 4 + 3 * 4 + 3 * 2 * 4,
        present_mask,
        0);
    set_descriptor(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS,
        ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
        ACGC_GX_CANONICAL_GEOMETRY_POS_XYZ,
        ACGC_GX_CANONICAL_GEOMETRY_COMP_F32,
        0,
        3,
        position_offset,
        3,
        0,
        0,
        0);
    set_descriptor(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0,
        ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
        ACGC_GX_CANONICAL_GEOMETRY_CLR_RGBA,
        ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA8,
        0,
        1,
        color_offset,
        3,
        0,
        0,
        0);
    set_descriptor(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0,
        ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
        ACGC_GX_CANONICAL_GEOMETRY_TEX_ST,
        ACGC_GX_CANONICAL_GEOMETRY_COMP_F32,
        0,
        2,
        texcoord_offset,
        3,
        0,
        0,
        0);

    put_record_word(position_offset, 12, 0, 0, bits_from_float(0.0f));
    put_record_word(position_offset, 12, 0, 1, bits_from_float(0.75f));
    put_record_word(position_offset, 12, 0, 2, bits_from_float(0.0f));
    put_record_word(position_offset, 12, 1, 0, bits_from_float(-0.75f));
    put_record_word(position_offset, 12, 1, 1, bits_from_float(-0.65f));
    put_record_word(position_offset, 12, 1, 2, bits_from_float(0.0f));
    put_record_word(position_offset, 12, 2, 0, bits_from_float(0.75f));
    put_record_word(position_offset, 12, 2, 1, bits_from_float(-0.65f));
    put_record_word(position_offset, 12, 2, 2, bits_from_float(0.0f));

    put_record_word(color_offset, 4, 0, 0, UINT32_C(0xFF0000FF));
    put_record_word(color_offset, 4, 1, 0, UINT32_C(0x00FF00FF));
    put_record_word(color_offset, 4, 2, 0, UINT32_C(0x0000FFFF));

    put_record_word(texcoord_offset, 8, 0, 0, bits_from_float(0.0f));
    put_record_word(texcoord_offset, 8, 0, 1, bits_from_float(0.0f));
    put_record_word(texcoord_offset, 8, 1, 0, bits_from_float(1.0f));
    put_record_word(texcoord_offset, 8, 1, 1, bits_from_float(0.0f));
    put_record_word(texcoord_offset, 8, 2, 0, bits_from_float(0.5f));
    put_record_word(texcoord_offset, 8, 2, 1, bits_from_float(1.0f));
}

static uint32_t make_indexed_triangle(uint32_t vcd_type) {
    const uint32_t present_mask =
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0);
    const uint32_t indexed_mask =
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS;
    const uint32_t position_offset =
        ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET;
    const uint32_t color_offset = position_offset + 2 * 3 * 4;
    const uint32_t index_offset = color_offset + 3 * 4;
    const uint32_t index_stride =
        vcd_type == ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX8 ? 1 : 2;
    const uint32_t stream_bytes = 2 * 3 * 4 + 3 * 4 + 3 * index_stride;

    set_header(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES,
        3,
        stream_bytes,
        present_mask,
        indexed_mask);
    set_descriptor(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS,
        vcd_type,
        ACGC_GX_CANONICAL_GEOMETRY_POS_XYZ,
        ACGC_GX_CANONICAL_GEOMETRY_COMP_F32,
        0,
        3,
        position_offset,
        2,
        index_offset,
        index_stride,
        3);
    set_descriptor(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0,
        ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
        ACGC_GX_CANONICAL_GEOMETRY_CLR_RGBA,
        ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA8,
        0,
        1,
        color_offset,
        3,
        0,
        0,
        0);

    put_record_word(position_offset, 12, 0, 0, bits_from_float(0.0f));
    put_record_word(position_offset, 12, 0, 1, bits_from_float(0.0f));
    put_record_word(position_offset, 12, 0, 2, bits_from_float(0.0f));
    put_record_word(position_offset, 12, 1, 0, bits_from_float(1.0f));
    put_record_word(position_offset, 12, 1, 1, bits_from_float(1.0f));
    put_record_word(position_offset, 12, 1, 2, bits_from_float(0.0f));
    put_record_word(color_offset, 4, 0, 0, UINT32_C(0xFF0000FF));
    put_record_word(color_offset, 4, 1, 0, UINT32_C(0x00FF00FF));
    put_record_word(color_offset, 4, 2, 0, UINT32_C(0x0000FFFF));
    if (index_stride == 1) {
        section_bytes[index_offset + 0] = 0;
        section_bytes[index_offset + 1] = 1;
        section_bytes[index_offset + 2] = 0;
    } else {
        put_le16(section_bytes + index_offset + 0, 0);
        put_le16(section_bytes + index_offset + 2, 1);
        put_le16(section_bytes + index_offset + 4, 0);
    }
    return ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET + stream_bytes;
}

static void make_matrix_triangle(void) {
    const uint32_t present_mask =
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS);
    const uint32_t matrix_offset =
        ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET;
    const uint32_t position_offset = matrix_offset + 3 * 4;

    set_header(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES,
        3,
        3 * 4 + 3 * 3 * 4,
        present_mask,
        0);
    set_descriptor(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX,
        ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
        0,
        0,
        0,
        1,
        matrix_offset,
        3,
        0,
        0,
        0);
    set_descriptor(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS,
        ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
        ACGC_GX_CANONICAL_GEOMETRY_POS_XYZ,
        ACGC_GX_CANONICAL_GEOMETRY_COMP_F32,
        0,
        3,
        position_offset,
        3,
        0,
        0,
        0);
    put_record_word(matrix_offset, 4, 0, 0, 0);
    put_record_word(matrix_offset, 4, 1, 0, 3);
    put_record_word(matrix_offset, 4, 2, 0, 6);
    put_record_word(position_offset, 12, 0, 0, bits_from_float(0.0f));
    put_record_word(position_offset, 12, 0, 1, bits_from_float(0.0f));
    put_record_word(position_offset, 12, 0, 2, bits_from_float(0.0f));
    put_record_word(position_offset, 12, 1, 0, bits_from_float(1.0f));
    put_record_word(position_offset, 12, 1, 1, bits_from_float(0.0f));
    put_record_word(position_offset, 12, 1, 2, bits_from_float(0.0f));
    put_record_word(position_offset, 12, 2, 0, bits_from_float(0.0f));
    put_record_word(position_offset, 12, 2, 1, bits_from_float(1.0f));
    put_record_word(position_offset, 12, 2, 2, bits_from_float(0.0f));
}

static void fill_dependencies(
    AcgcGxCanonicalGeometryDependencyResults* dependencies
) {
    memset(dependencies, 0, sizeof(*dependencies));
    dependencies->transform_valid = 1;
    dependencies->texgens_valid = 1;
    dependencies->transform_position_known_mask = 0x3FF;
    dependencies->transform_normal_known_mask = 0x3FF;
    dependencies->transform_current_position_known = 1;
    dependencies->transform_current_position_id = 0;
    dependencies->texgen_present_mask = 1;
    dependencies->texgen_ordinary_known_mask = 1u << 10;
    dependencies->texgen_post_known_mask = 1u << 20;
    dependencies->texgen_selector[0] = 60;
}

static int accepts_exact_layout_and_direct_values(void) {
    AcgcGxCanonicalGeometryState prefix;

    CHECK(sizeof(prefix) == 0x6B0);
    CHECK(_Alignof(AcgcGxCanonicalGeometryState) == 4);
    CHECK(offsetof(AcgcGxCanonicalGeometryDescriptor, value_offset) == 0x18);
    CHECK(offsetof(AcgcGxCanonicalGeometryDescriptor, index_offset) == 0x28);
    CHECK(ACGC_GX_CANONICAL_GEOMETRY_SECTION_ID == 1);
    CHECK(ACGC_GX_CANONICAL_GEOMETRY_SECTION_MASK == UINT32_C(0x0001));
    CHECK(ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE == 0x6B0);
    CHECK(ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE == 0x10000);

    make_direct_triangle();
    CHECK(acgc_gx_canonical_geometry_state_validate(
        section_bytes, 0x6F8));
    put_descriptor_word(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_FRACTION_OFFSET,
        31);
    put_descriptor_word(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_FRACTION_OFFSET,
        17);
    put_descriptor_word(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_FRACTION_OFFSET,
        29);
    CHECK(acgc_gx_canonical_geometry_state_validate(
        section_bytes, 0x6F8));
    CHECK(acgc_gx_canonical_geometry_validate(section_bytes, 0x10000));
    section_bytes[0x6F8] = 1;
    CHECK(!acgc_gx_canonical_geometry_state_validate(
        section_bytes, 0x10000));
    section_bytes[0x6F8] = 0;
    CHECK(!acgc_gx_canonical_geometry_state_validate(
        section_bytes, 0x6F7));
    CHECK(!acgc_gx_canonical_geometry_state_validate(
        section_bytes, 0x10001));
    return 1;
}

static int accepts_index8_and_index16_wire_rules(void) {
    uint32_t end;

    end = make_indexed_triangle(
        ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX8);
    CHECK(end == 0x6D7);
    CHECK(acgc_gx_canonical_geometry_state_validate(section_bytes, end));
    section_bytes[0x6D5] = 2;
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, end));

    end = make_indexed_triangle(
        ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX16);
    CHECK(end == 0x6DA);
    CHECK(acgc_gx_canonical_geometry_state_validate(section_bytes, end));
    section_bytes[0x6D5] = 1;
    section_bytes[0x6D6] = 0;
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, end));

    end = make_indexed_triangle(
        ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX8);
    section_bytes[0x6D4] = 1;
    section_bytes[0x6D5] = 0;
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, end));
    end = make_indexed_triangle(
        ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX8);
    section_bytes[0x6D4] = 0;
    section_bytes[0x6D5] = 0;
    section_bytes[0x6D6] = 0;
    put_record_word(ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET, 12, 1, 0,
                    bits_from_float(0.0f));
    put_record_word(ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET, 12, 1, 1,
                    bits_from_float(0.0f));
    put_record_word(ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET, 12, 1, 2,
                    bits_from_float(0.0f));
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, end));
    return 1;
}

static int rejects_malformed_descriptors_and_topology(void) {
    uint32_t present_mask;

    make_direct_triangle();
    present_mask =
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0) |
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0);

    put_descriptor_word(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_ENCODING_OFFSET,
        0);
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, 0x6F8));
    make_direct_triangle();
    put_descriptor_word(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET + 4);
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, 0x6F8));

    make_direct_triangle();
    put_le32(section_bytes + ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET + 36,
             UINT32_C(0x11223300));
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, 0x6F8));

    make_direct_triangle();
    put_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRESENT_MASK_OFFSET,
        present_mask & ~(UINT32_C(1) <<
            ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0));
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, 0x6F8));

    make_direct_triangle();
    put_descriptor_word(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS_MTX_ARRAY,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VCD_TYPE_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT);
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, 0x6F8));

    make_direct_triangle();
    put_descriptor_word(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VCD_TYPE_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX8);
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, 0x6F8));

    make_direct_triangle();
    put_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRIMITIVE_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES);
    put_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_VERTEX_COUNT_OFFSET,
        4);
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, 0x6F8));

    make_direct_triangle();
    put_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRIMITIVE_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS);
    put_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_VERTEX_COUNT_OFFSET,
        4);
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, 0x6F8));
    return 1;
}

static int rejects_nbt3_and_indexed_nbt(void) {
    const uint32_t nbt_mask =
        UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT;
    const uint32_t position_offset =
        ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET;
    const uint32_t nbt_offset = position_offset + 3 * 3 * 4;
    uint32_t word;

    set_header(
        ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES,
        3,
        3 * 3 * 4 + 3 * 9 * 4,
        (UINT32_C(1) << ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS) | nbt_mask,
        0);
    set_descriptor(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS,
        ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
        ACGC_GX_CANONICAL_GEOMETRY_POS_XYZ,
        ACGC_GX_CANONICAL_GEOMETRY_COMP_F32,
        0,
        3,
        position_offset,
        3,
        0,
        0,
        0);
    set_descriptor(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT,
        ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT,
        ACGC_GX_CANONICAL_GEOMETRY_NRM_NBT,
        ACGC_GX_CANONICAL_GEOMETRY_COMP_S16,
        0,
        9,
        nbt_offset,
        3,
        0,
        0,
        0);
    for (word = 0; word < 9; word++) {
        put_record_word(nbt_offset, 36, 0, word, bits_from_float(1.0f));
        put_record_word(nbt_offset, 36, 1, word, bits_from_float(0.0f));
        put_record_word(nbt_offset, 36, 2, word, bits_from_float(-1.0f));
    }
    CHECK(acgc_gx_canonical_geometry_state_validate(section_bytes, 0x740));

    put_descriptor_word(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT,
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_COUNT_OFFSET,
        ACGC_GX_CANONICAL_GEOMETRY_NRM_NBT3);
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, 0x740));

    set_descriptor(
        ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT,
        ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX8,
        ACGC_GX_CANONICAL_GEOMETRY_NRM_NBT,
        ACGC_GX_CANONICAL_GEOMETRY_COMP_S16,
        0,
        9,
        nbt_offset,
        3,
        nbt_offset + 3 * 9 * 4,
        1,
        3);
    put_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_INDEXED_MASK_OFFSET,
        nbt_mask);
    put_le32(
        section_bytes + ACGC_GX_CANONICAL_GEOMETRY_HEADER_STREAM_BYTES_OFFSET,
        3 * 3 * 4 + 3 * 9 * 4 + 3);
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, 0x743));
    return 1;
}

static int accepts_matrix_values_and_rejects_bad_ids(void) {
    AcgcGxCanonicalGeometryDependencyResults dependencies;

    make_matrix_triangle();
    CHECK(acgc_gx_canonical_geometry_state_validate(section_bytes, 0x6E0));
    fill_dependencies(&dependencies);
    dependencies.texgens_valid = 0;
    CHECK(acgc_gx_canonical_geometry_state_validate_dependencies(
        section_bytes, 0x6E0, &dependencies));

    put_le32(section_bytes + ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET, 1);
    CHECK(!acgc_gx_canonical_geometry_state_validate(section_bytes, 0x6E0));
    return 1;
}

static int accepts_dependency_defaults_and_rejects_unknowns(void) {
    AcgcGxCanonicalGeometryDependencyResults dependencies;

    make_direct_triangle();
    fill_dependencies(&dependencies);
    CHECK(acgc_gx_canonical_geometry_state_validate_dependencies(
        section_bytes, 0x6F8, &dependencies));

    dependencies.texgen_selector[0] = 0;
    CHECK(!acgc_gx_canonical_geometry_state_validate_dependencies(
        section_bytes, 0x6F8, &dependencies));
    dependencies.texgen_selector[0] = 60;
    dependencies.texgen_ordinary_known_mask = 0;
    CHECK(!acgc_gx_canonical_geometry_state_validate_dependencies(
        section_bytes, 0x6F8, &dependencies));

    fill_dependencies(&dependencies);
    dependencies.texgen_selector[0] = 125;
    CHECK(acgc_gx_canonical_geometry_state_validate_dependencies(
        section_bytes, 0x6F8, &dependencies));
    dependencies.texgen_post_known_mask = 0;
    CHECK(!acgc_gx_canonical_geometry_state_validate_dependencies(
        section_bytes, 0x6F8, &dependencies));

    fill_dependencies(&dependencies);
    dependencies.transform_current_position_known = 0;
    CHECK(!acgc_gx_canonical_geometry_state_validate_dependencies(
        section_bytes, 0x6F8, &dependencies));
    dependencies.transform_current_position_known = 1;
    dependencies.required_channel_mask = 1;
    CHECK(!acgc_gx_canonical_geometry_state_validate_dependencies(
        section_bytes, 0x6F8, &dependencies));
    dependencies.channels_valid = 1;
    dependencies.required_lighting_mask = 2;
    dependencies.lighting_valid = 1;
    dependencies.lighting_loaded_mask = 2;
    CHECK(acgc_gx_canonical_geometry_state_validate_dependencies(
        section_bytes, 0x6F8, &dependencies));
    dependencies.lighting_loaded_mask = 0;
    CHECK(!acgc_gx_canonical_geometry_state_validate_dependencies(
        section_bytes, 0x6F8, &dependencies));
    return 1;
}

static int accepts_canonical_decoding_rules(void) {
    uint32_t word;

    CHECK(acgc_gx_canonical_geometry_decode_scalar_word(
        ACGC_GX_CANONICAL_GEOMETRY_COMP_U8,
        0,
        1,
        &word));
    CHECK(word == UINT32_C(0x3F800000));
    CHECK(acgc_gx_canonical_geometry_decode_scalar_word(
        ACGC_GX_CANONICAL_GEOMETRY_COMP_U16,
        1,
        2,
        &word));
    CHECK(word == UINT32_C(0x3F800000));
    CHECK(acgc_gx_canonical_geometry_decode_scalar_word(
        ACGC_GX_CANONICAL_GEOMETRY_COMP_F32,
        31,
        UINT32_C(0x3F800000),
        &word));
    CHECK(word == UINT32_C(0x3F800000));
    CHECK(!acgc_gx_canonical_geometry_decode_scalar_word(
        ACGC_GX_CANONICAL_GEOMETRY_COMP_F32,
        0,
        UINT32_C(0x7FC00000),
        &word));
    CHECK(acgc_gx_canonical_geometry_decode_normal_word(
        ACGC_GX_CANONICAL_GEOMETRY_COMP_S8,
        0x7F,
        &word));
    CHECK(word == UINT32_C(0x3F800000));
    CHECK(acgc_gx_canonical_geometry_decode_normal_word(
        ACGC_GX_CANONICAL_GEOMETRY_COMP_S16,
        0x8001,
        &word));
    CHECK(word == UINT32_C(0xBF800000));
    CHECK(acgc_gx_canonical_geometry_decode_normal_word(
        ACGC_GX_CANONICAL_GEOMETRY_COMP_F32,
        UINT32_C(0x3F800000),
        &word));
    CHECK(word == UINT32_C(0x3F800000));

    CHECK(acgc_gx_canonical_geometry_decode_color_word(
        ACGC_GX_CANONICAL_GEOMETRY_CLR_RGB,
        ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGB565,
        UINT32_C(0xF800),
        &word));
    CHECK(word == UINT32_C(0xFF0000FF));
    CHECK(acgc_gx_canonical_geometry_decode_color_word(
        ACGC_GX_CANONICAL_GEOMETRY_CLR_RGB,
        ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGB8,
        UINT32_C(0x112233),
        &word));
    CHECK(word == UINT32_C(0x112233FF));
    CHECK(acgc_gx_canonical_geometry_decode_color_word(
        ACGC_GX_CANONICAL_GEOMETRY_CLR_RGB,
        ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBX8,
        UINT32_C(0x11223344),
        &word));
    CHECK(word == UINT32_C(0x112233FF));
    CHECK(acgc_gx_canonical_geometry_decode_color_word(
        ACGC_GX_CANONICAL_GEOMETRY_CLR_RGBA,
        ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA4,
        UINT32_C(0xF123),
        &word));
    CHECK(word == UINT32_C(0xFF112233));
    CHECK(acgc_gx_canonical_geometry_decode_color_word(
        ACGC_GX_CANONICAL_GEOMETRY_CLR_RGBA,
        ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA6,
        UINT32_C(0xFFFFFF),
        &word));
    CHECK(word == UINT32_C(0xFFFFFFFF));
    CHECK(acgc_gx_canonical_geometry_decode_color_word(
        ACGC_GX_CANONICAL_GEOMETRY_CLR_RGBA,
        ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA8,
        UINT32_C(0x12345678),
        &word));
    CHECK(word == UINT32_C(0x12345678));
    CHECK(!acgc_gx_canonical_geometry_decode_color_word(
        ACGC_GX_CANONICAL_GEOMETRY_CLR_RGB,
        ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA8,
        UINT32_C(0),
        &word));
    return 1;
}

static int accepts_geometry_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    CHECK(acgc_gx_canonical_geometry_metadata_validate(
        &envelope, sizeof(envelope)));
    envelope.header.present_state_mask =
        ACGC_GX_CANONICAL_GEOMETRY_SECTION_MASK;
    envelope.header.required_state_mask =
        ACGC_GX_CANONICAL_GEOMETRY_SECTION_MASK;
    envelope.header.payload_byte_size =
        ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE;
    envelope.header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        envelope.header.payload_byte_size;
    entry = &envelope.directory[
        ACGC_GX_CANONICAL_GEOMETRY_SECTION_ID - 1];
    entry->section_version = ACGC_GX_CANONICAL_GEOMETRY_STATE_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE;
    entry->count = 1;
    entry->capacity = 1;
    entry->valid_mask = ACGC_GX_CANONICAL_GEOMETRY_SECTION_MASK;
    CHECK(acgc_gx_canonical_geometry_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    entry->byte_size = ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE;
    envelope.header.payload_byte_size = entry->byte_size;
    envelope.header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        envelope.header.payload_byte_size;
    CHECK(acgc_gx_canonical_geometry_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    return 1;
}

int main(void) {
    CHECK(accepts_exact_layout_and_direct_values());
    CHECK(accepts_index8_and_index16_wire_rules());
    CHECK(rejects_malformed_descriptors_and_topology());
    CHECK(rejects_nbt3_and_indexed_nbt());
    CHECK(accepts_matrix_values_and_rejects_bad_ids());
    CHECK(accepts_dependency_defaults_and_rejects_unknowns());
    CHECK(accepts_canonical_decoding_rules());
    CHECK(accepts_geometry_metadata());
    puts("GX canonical Geometry tests: PASS (fixed prefix, LE streams, validation)");
    return 0;
}
