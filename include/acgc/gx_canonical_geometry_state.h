#ifndef ACGC_GX_CANONICAL_GEOMETRY_STATE_H
#define ACGC_GX_CANONICAL_GEOMETRY_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical Geometry is a value-only, little-endian byte section.  The
 * fixed-width C structs below describe its 0x6B0-byte prefix; the variable
 * stream follows immediately in the section and is validated through the
 * explicit byte view below.  Section ID/version/mask/count/capacity belong to
 * the cumulative envelope; no host pointer, GL object, cache address, or
 * guest/display-list address is part of this ABI.
 *
 * The prefix is intentionally independent of the legacy V1-V4 semantic
 * packets.  It describes one bounded draw: triangles or quads only, with a
 * maximum of 128 vertices.  The current PC cache is not a producer for this
 * section until its VCD/VAT knownness and source ownership are repaired.
 */
#define ACGC_GX_CANONICAL_GEOMETRY_STATE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_GEOMETRY_SECTION_ID UINT32_C(1)
#define ACGC_GX_CANONICAL_GEOMETRY_SECTION_MASK UINT32_C(0x0001)
#define ACGC_GX_CANONICAL_GEOMETRY_STATE_COUNT UINT32_C(1)
#define ACGC_GX_CANONICAL_GEOMETRY_STATE_CAPACITY UINT32_C(1)

#define ACGC_GX_CANONICAL_GEOMETRY_HEADER_SIZE UINT32_C(48)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE UINT32_C(64)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT UINT32_C(26)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_BYTES \
    (ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT * \
     ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_OFFSET \
    ACGC_GX_CANONICAL_GEOMETRY_HEADER_SIZE
#define ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET \
    (ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_OFFSET + \
     (ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT * \
      ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE))
#define ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE \
    ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET
#define ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE UINT32_C(0x10000)
#define ACGC_GX_CANONICAL_GEOMETRY_MAX_STREAM_BYTES \
    (ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE - \
     ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET)
#define ACGC_GX_CANONICAL_GEOMETRY_ALIGNMENT UINT32_C(4)
#define ACGC_GX_CANONICAL_GEOMETRY_MAX_VERTEX_COUNT UINT32_C(128)
#define ACGC_GX_CANONICAL_GEOMETRY_VALID_ATTRIBUTE_MASK UINT32_C(0x03FFFFFF)
#define ACGC_GX_CANONICAL_GEOMETRY_VTXFMT_COUNT UINT32_C(8)

/* Primitive values are canonical Geometry values, not GX hardware values. */
#define ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_TRIANGLES UINT32_C(1)
#define ACGC_GX_CANONICAL_GEOMETRY_PRIMITIVE_QUADS UINT32_C(2)

/* GX_VA_* slot order, including the four deliberately absent array slots. */
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX UINT32_C(0)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0MTXIDX UINT32_C(1)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX1MTXIDX UINT32_C(2)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX2MTXIDX UINT32_C(3)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX3MTXIDX UINT32_C(4)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX4MTXIDX UINT32_C(5)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX5MTXIDX UINT32_C(6)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX6MTXIDX UINT32_C(7)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX7MTXIDX UINT32_C(8)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS UINT32_C(9)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM UINT32_C(10)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR0 UINT32_C(11)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_CLR1 UINT32_C(12)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0 UINT32_C(13)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX1 UINT32_C(14)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX2 UINT32_C(15)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX3 UINT32_C(16)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX4 UINT32_C(17)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX5 UINT32_C(18)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX6 UINT32_C(19)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX7 UINT32_C(20)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_POS_MTX_ARRAY UINT32_C(21)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_NRM_MTX_ARRAY UINT32_C(22)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX_MTX_ARRAY UINT32_C(23)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_LIGHT_ARRAY UINT32_C(24)
#define ACGC_GX_CANONICAL_GEOMETRY_ATTR_NBT UINT32_C(25)

#define ACGC_GX_CANONICAL_GEOMETRY_MATRIX_ATTR_FIRST \
    ACGC_GX_CANONICAL_GEOMETRY_ATTR_PNMTXIDX
#define ACGC_GX_CANONICAL_GEOMETRY_MATRIX_ATTR_LAST \
    ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX7MTXIDX
#define ACGC_GX_CANONICAL_GEOMETRY_TEX_ATTR_FIRST \
    ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX0
#define ACGC_GX_CANONICAL_GEOMETRY_TEX_ATTR_LAST \
    ACGC_GX_CANONICAL_GEOMETRY_ATTR_TEX7

/* GXAttrType values.  Matrix VCD values are reduced to their effective bit. */
#define ACGC_GX_CANONICAL_GEOMETRY_VCD_NONE UINT32_C(0)
#define ACGC_GX_CANONICAL_GEOMETRY_VCD_DIRECT UINT32_C(1)
#define ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX8 UINT32_C(2)
#define ACGC_GX_CANONICAL_GEOMETRY_VCD_INDEX16 UINT32_C(3)

/* GXCompCnt values. */
#define ACGC_GX_CANONICAL_GEOMETRY_POS_XY UINT32_C(0)
#define ACGC_GX_CANONICAL_GEOMETRY_POS_XYZ UINT32_C(1)
#define ACGC_GX_CANONICAL_GEOMETRY_NRM_XYZ UINT32_C(0)
#define ACGC_GX_CANONICAL_GEOMETRY_NRM_NBT UINT32_C(1)
#define ACGC_GX_CANONICAL_GEOMETRY_NRM_NBT3 UINT32_C(2)
#define ACGC_GX_CANONICAL_GEOMETRY_CLR_RGB UINT32_C(0)
#define ACGC_GX_CANONICAL_GEOMETRY_CLR_RGBA UINT32_C(1)
#define ACGC_GX_CANONICAL_GEOMETRY_TEX_S UINT32_C(0)
#define ACGC_GX_CANONICAL_GEOMETRY_TEX_ST UINT32_C(1)

/* GXCompType scalar values. */
#define ACGC_GX_CANONICAL_GEOMETRY_COMP_U8 UINT32_C(0)
#define ACGC_GX_CANONICAL_GEOMETRY_COMP_S8 UINT32_C(1)
#define ACGC_GX_CANONICAL_GEOMETRY_COMP_U16 UINT32_C(2)
#define ACGC_GX_CANONICAL_GEOMETRY_COMP_S16 UINT32_C(3)
#define ACGC_GX_CANONICAL_GEOMETRY_COMP_F32 UINT32_C(4)

/* GXCompType color-packing values. */
#define ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGB565 UINT32_C(0)
#define ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGB8 UINT32_C(1)
#define ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBX8 UINT32_C(2)
#define ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA4 UINT32_C(3)
#define ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA6 UINT32_C(4)
#define ACGC_GX_CANONICAL_GEOMETRY_COLOR_RGBA8 UINT32_C(5)

/* Exact logical matrix IDs; malformed IDs are never divided or floored. */
#define ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_FIRST UINT32_C(0)
#define ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_STRIDE UINT32_C(3)
#define ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_LAST UINT32_C(27)
#define ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_COUNT UINT32_C(10)
#define ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_FIRST UINT32_C(30)
#define ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_STRIDE UINT32_C(3)
#define ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_LAST UINT32_C(60)
#define ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_COUNT UINT32_C(11)
#define ACGC_GX_CANONICAL_GEOMETRY_POST_TEX_MATRIX_ID_FIRST UINT32_C(64)
#define ACGC_GX_CANONICAL_GEOMETRY_POST_TEX_MATRIX_ID_STRIDE UINT32_C(3)
#define ACGC_GX_CANONICAL_GEOMETRY_POST_TEX_MATRIX_ID_LAST UINT32_C(121)
#define ACGC_GX_CANONICAL_GEOMETRY_POST_IDENTITY UINT32_C(125)
#define ACGC_GX_CANONICAL_GEOMETRY_POST_TEX_MATRIX_ID_COUNT UINT32_C(20)
#define ACGC_GX_CANONICAL_GEOMETRY_POST_RECORD_COUNT UINT32_C(21)

/* Fixed header word offsets, in the frozen byte-for-byte order. */
#define ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRIMITIVE_OFFSET UINT32_C(0x00)
#define ACGC_GX_CANONICAL_GEOMETRY_HEADER_VERTEX_COUNT_OFFSET UINT32_C(0x04)
#define ACGC_GX_CANONICAL_GEOMETRY_HEADER_VTXFMT_OFFSET UINT32_C(0x08)
#define ACGC_GX_CANONICAL_GEOMETRY_HEADER_DESCRIPTOR_COUNT_OFFSET \
    UINT32_C(0x0C)
#define ACGC_GX_CANONICAL_GEOMETRY_HEADER_PRESENT_MASK_OFFSET UINT32_C(0x10)
#define ACGC_GX_CANONICAL_GEOMETRY_HEADER_INDEXED_MASK_OFFSET UINT32_C(0x14)
#define ACGC_GX_CANONICAL_GEOMETRY_HEADER_DESCRIPTOR_OFFSET_OFFSET \
    UINT32_C(0x18)
#define ACGC_GX_CANONICAL_GEOMETRY_HEADER_DESCRIPTOR_BYTES_OFFSET \
    UINT32_C(0x1C)
#define ACGC_GX_CANONICAL_GEOMETRY_HEADER_STREAM_OFFSET_OFFSET UINT32_C(0x20)
#define ACGC_GX_CANONICAL_GEOMETRY_HEADER_STREAM_BYTES_OFFSET UINT32_C(0x24)
#define ACGC_GX_CANONICAL_GEOMETRY_HEADER_RESERVED0_OFFSET UINT32_C(0x28)
#define ACGC_GX_CANONICAL_GEOMETRY_HEADER_RESERVED1_OFFSET UINT32_C(0x2C)

/* Fixed descriptor word offsets, in the frozen ABI order. */
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VCD_TYPE_OFFSET UINT32_C(0x00)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_COUNT_OFFSET UINT32_C(0x04)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_TYPE_OFFSET UINT32_C(0x08)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VAT_FRACTION_OFFSET \
    UINT32_C(0x0C)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_ENCODING_OFFSET \
    UINT32_C(0x10)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_CANONICAL_WORD_COUNT_OFFSET \
    UINT32_C(0x14)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_OFFSET UINT32_C(0x18)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_BYTES_OFFSET \
    UINT32_C(0x1C)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_STRIDE_OFFSET \
    UINT32_C(0x20)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_VALUE_COUNT_OFFSET \
    UINT32_C(0x24)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_OFFSET UINT32_C(0x28)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_BYTES_OFFSET \
    UINT32_C(0x2C)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_STRIDE_OFFSET \
    UINT32_C(0x30)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_INDEX_COUNT_OFFSET \
    UINT32_C(0x34)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_RESERVED0_OFFSET UINT32_C(0x38)
#define ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_RESERVED1_OFFSET UINT32_C(0x3C)

typedef struct AcgcGxCanonicalGeometryHeader {
    uint32_t primitive;
    uint32_t vertex_count;
    uint32_t vtxfmt;
    uint32_t descriptor_count;
    uint32_t present_mask;
    uint32_t indexed_mask;
    uint32_t descriptor_offset;
    uint32_t descriptor_bytes;
    uint32_t stream_offset;
    uint32_t stream_bytes;
    uint32_t reserved[2];
} AcgcGxCanonicalGeometryHeader;

typedef struct AcgcGxCanonicalGeometryDescriptor {
    uint32_t vcd_type;
    uint32_t vat_count;
    uint32_t vat_type;
    uint32_t vat_fraction;
    uint32_t value_encoding;
    uint32_t canonical_word_count;
    uint32_t value_offset;
    uint32_t value_bytes;
    uint32_t value_stride;
    uint32_t value_count;
    uint32_t index_offset;
    uint32_t index_bytes;
    uint32_t index_stride;
    uint32_t index_count;
    uint32_t reserved[2];
} AcgcGxCanonicalGeometryDescriptor;

/* The stream starts after this pointer-free, fixed-width prefix. */
typedef struct AcgcGxCanonicalGeometryState {
    AcgcGxCanonicalGeometryHeader header;
    AcgcGxCanonicalGeometryDescriptor descriptor[
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_COUNT];
} AcgcGxCanonicalGeometryState;

/*
 * These are validation results supplied by later cumulative-section owners,
 * not serialized Geometry fields.  They make cross-section dependencies
 * explicit without importing a Transform, Texgen, Channels, Lighting, or
 * bump-state pointer into the Geometry ABI.
 *
 * Matrix-known masks use logical record numbers: Transform position/normal
 * records 0..9, Texgen ordinary records 0..10 (IDs 30..60), and Texgen post
 * records 0..20 (IDs 64..121 and 125).  Selector values themselves remain
 * exact logical IDs; no division or flooring is permitted.
 */
typedef struct AcgcGxCanonicalGeometryDependencyResults {
    uint32_t transform_valid;
    uint32_t texgens_valid;
    uint32_t channels_valid;
    uint32_t lighting_valid;
    uint32_t bump_valid;
    uint32_t reserved0;
    uint32_t required_geometry_present_mask;
    uint32_t required_channel_mask;
    uint32_t required_lighting_mask;
    uint32_t required_bump_mask;
    uint32_t transform_position_known_mask;
    uint32_t transform_normal_known_mask;
    uint32_t transform_current_position_known;
    uint32_t transform_current_position_id;
    uint32_t texgen_present_mask;
    uint32_t texgen_ordinary_known_mask;
    uint32_t texgen_post_known_mask;
    uint32_t texgen_selector[8];
    uint32_t lighting_loaded_mask;
    uint32_t bump_known_mask;
    uint32_t reserved[4];
} AcgcGxCanonicalGeometryDependencyResults;

/*
 * Validate a complete little-endian section byte view.  section_byte_size is
 * the exact supplied section extent and stream_bytes must equal that extent
 * minus 0x6B0; valid extents are inclusive in [0x6B0, 0x10000].  The
 * standalone validator does not require an envelope or any host-owned state.
 */
int acgc_gx_canonical_geometry_state_validate(
    const uint8_t* section_bytes,
    size_t section_byte_size
);

/* Spelled-out alias for callers that use the section rather than state name. */
int acgc_gx_canonical_geometry_validate(
    const uint8_t* section_bytes,
    size_t section_byte_size
);

/* Validate the same section plus Transform/Texgen/Channels/Lighting/bump results. */
int acgc_gx_canonical_geometry_state_validate_dependencies(
    const uint8_t* section_bytes,
    size_t section_byte_size,
    const AcgcGxCanonicalGeometryDependencyResults* dependencies
);

/* Validate the cumulative envelope entry for the variable-size Geometry section. */
int acgc_gx_canonical_geometry_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

/*
 * Canonical scalar decoding helpers used by a future producer.  raw_value is
 * the unsigned source field widened to uint32_t for U8/S8/U16/S16, or the
 * original binary32 bit pattern for F32.  Integer positions/texcoords use
 * raw / 2^vat_fraction.  The fraction argument is intentionally ignored for
 * F32. Normal and packed-color descriptors store their hardware-ignored VAT
 * argument canonically as zero. Normal S8/S16 values use raw / 127 and raw /
 * 32767 respectively; conversion is round-to-nearest-even to a binary32 word.
 */
int acgc_gx_canonical_geometry_decode_scalar_word(
    uint32_t vat_type,
    uint32_t vat_fraction,
    uint32_t raw_value,
    uint32_t* canonical_word
);

int acgc_gx_canonical_geometry_decode_normal_word(
    uint32_t vat_type,
    uint32_t raw_value,
    uint32_t* canonical_word
);

/*
 * Color decoding returns logical RGBA8 packing R[7:0], G[15:8], B[23:16],
 * A[31:24].  raw_value uses the guest/source format's packed bits:
 * RGB565/RGBA4/RGBA6 use their low 16/16/24 bits, RGB8 uses R[23:16],
 * G[15:8], B[7:0], RGBX8 and RGBA8 use R[31:24] through the low byte.
 * RGB/RGBX alpha is the canonical default 255; RGBX's X byte is ignored.
 */
int acgc_gx_canonical_geometry_decode_color_word(
    uint32_t vat_count,
    uint32_t vat_type,
    uint32_t raw_value,
    uint32_t* canonical_word
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_GEOMETRY_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_GEOMETRY_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    sizeof(uint32_t) == 4,
    "canonical GX Geometry requires 32-bit uint32_t"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalGeometryHeader) ==
        ACGC_GX_CANONICAL_GEOMETRY_HEADER_SIZE,
    "canonical GX Geometry header size changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    ACGC_GX_CANONICAL_GEOMETRY_ALIGNOF(AcgcGxCanonicalGeometryHeader) == 4,
    "canonical GX Geometry header alignment changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalGeometryDescriptor) ==
        ACGC_GX_CANONICAL_GEOMETRY_DESCRIPTOR_SIZE,
    "canonical GX Geometry descriptor size changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    ACGC_GX_CANONICAL_GEOMETRY_ALIGNOF(AcgcGxCanonicalGeometryDescriptor) == 4,
    "canonical GX Geometry descriptor alignment changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalGeometryState) ==
        ACGC_GX_CANONICAL_GEOMETRY_STREAM_OFFSET,
    "canonical GX Geometry prefix size changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryState, header) == 0x000,
    "canonical GX Geometry header offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryState, descriptor) == 0x030,
    "canonical GX Geometry descriptor prefix offset changed"
);

ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryHeader, primitive) == 0x00,
    "canonical GX Geometry primitive offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryHeader, vertex_count) == 0x04,
    "canonical GX Geometry vertex-count offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryHeader, vtxfmt) == 0x08,
    "canonical GX Geometry VTXFMT offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryHeader, descriptor_count) == 0x0C,
    "canonical GX Geometry descriptor-count offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryHeader, present_mask) == 0x10,
    "canonical GX Geometry present-mask offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryHeader, indexed_mask) == 0x14,
    "canonical GX Geometry indexed-mask offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryHeader, descriptor_offset) == 0x18,
    "canonical GX Geometry descriptor-offset offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryHeader, descriptor_bytes) == 0x1C,
    "canonical GX Geometry descriptor-bytes offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryHeader, stream_offset) == 0x20,
    "canonical GX Geometry stream-offset offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryHeader, stream_bytes) == 0x24,
    "canonical GX Geometry stream-bytes offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryHeader, reserved) == 0x28,
    "canonical GX Geometry header reserved offset changed"
);

ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, vcd_type) == 0x00,
    "canonical GX Geometry vcd-type offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, vat_count) == 0x04,
    "canonical GX Geometry vat-count offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, vat_type) == 0x08,
    "canonical GX Geometry vat-type offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, vat_fraction) == 0x0C,
    "canonical GX Geometry vat-fraction offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, value_encoding) == 0x10,
    "canonical GX Geometry value-encoding offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, canonical_word_count) == 0x14,
    "canonical GX Geometry word-count offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, value_offset) == 0x18,
    "canonical GX Geometry value-offset offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, value_bytes) == 0x1C,
    "canonical GX Geometry value-bytes offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, value_stride) == 0x20,
    "canonical GX Geometry value-stride offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, value_count) == 0x24,
    "canonical GX Geometry value-count offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, index_offset) == 0x28,
    "canonical GX Geometry index-offset offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, index_bytes) == 0x2C,
    "canonical GX Geometry index-bytes offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, index_stride) == 0x30,
    "canonical GX Geometry index-stride offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, index_count) == 0x34,
    "canonical GX Geometry index-count offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, reserved) == 0x38,
    "canonical GX Geometry reserved offset changed"
);
ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalGeometryDescriptor, reserved[1]) == 0x3C,
    "canonical GX Geometry second reserved offset changed"
);

#undef ACGC_GX_CANONICAL_GEOMETRY_ALIGNOF
#undef ACGC_GX_CANONICAL_GEOMETRY_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_GEOMETRY_STATE_H */
