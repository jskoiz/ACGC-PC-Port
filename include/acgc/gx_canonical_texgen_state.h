#ifndef ACGC_GX_CANONICAL_TEXGEN_STATE_H
#define ACGC_GX_CANONICAL_TEXGEN_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Renderer-neutral canonical GX Texgen/SU state.  The wire form is a fixed
 * sequence of little-endian uint32 words; these C structs are decoded values,
 * not a native-byte-order serialization.  Matrix values are IEEE-754
 * binary32 bit patterns.  No pointer, enum object, GL handle, or resource
 * lease crosses this boundary.
 */
#define ACGC_GX_CANONICAL_TEXGEN_STATE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE UINT32_C(0xA40)
#define ACGC_GX_CANONICAL_TEXGEN_STATE_ALIGNMENT UINT32_C(4)
#define ACGC_GX_CANONICAL_TEXGEN_STATE_COUNT UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXGEN_STATE_CAPACITY UINT32_C(1)

#define ACGC_GX_CANONICAL_TEXGEN_HEADER_WORD_COUNT UINT32_C(16)
#define ACGC_GX_CANONICAL_TEXGEN_HEADER_SIZE UINT32_C(0x040)

#define ACGC_GX_CANONICAL_TEXGEN_COUNT UINT32_C(8)
#define ACGC_GX_CANONICAL_TEXGEN_CAPACITY \
    ACGC_GX_CANONICAL_TEXGEN_COUNT
#define ACGC_GX_CANONICAL_TEXGEN_RECORD_WORD_COUNT UINT32_C(8)
#define ACGC_GX_CANONICAL_TEXGEN_RECORD_SIZE UINT32_C(0x020)
#define ACGC_GX_CANONICAL_TEXGEN_OFFSET UINT32_C(0x040)
#define ACGC_GX_CANONICAL_TEXGEN_KNOWN_MASK UINT32_C(0x000000FF)

#define ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT UINT32_C(11)
#define ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_CAPACITY \
    ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT
#define ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT UINT32_C(21)
#define ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_CAPACITY \
    ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT
#define ACGC_GX_CANONICAL_TEXGEN_MATRIX_RECORD_WORD_COUNT UINT32_C(16)
#define ACGC_GX_CANONICAL_TEXGEN_MATRIX_RECORD_SIZE UINT32_C(0x040)
#define ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_OFFSET UINT32_C(0x140)
#define ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_OFFSET UINT32_C(0x400)
#define ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_KNOWN_MASK \
    UINT32_C(0x000007FF)
#define ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_KNOWN_MASK \
    UINT32_C(0x001FFFFF)

#define ACGC_GX_CANONICAL_TEXGEN_SU_COUNT UINT32_C(8)
#define ACGC_GX_CANONICAL_TEXGEN_SU_CAPACITY \
    ACGC_GX_CANONICAL_TEXGEN_SU_COUNT
#define ACGC_GX_CANONICAL_TEXGEN_SU_RECORD_WORD_COUNT UINT32_C(8)
#define ACGC_GX_CANONICAL_TEXGEN_SU_RECORD_SIZE UINT32_C(0x020)
#define ACGC_GX_CANONICAL_TEXGEN_SU_OFFSET UINT32_C(0x940)
#define ACGC_GX_CANONICAL_TEXGEN_SU_KNOWN_MASK UINT32_C(0x000000FF)

#define ACGC_GX_CANONICAL_TEXGEN_SECTION_ID \
    ACGC_GX_CANONICAL_SECTION_ID_TEXGENS
#define ACGC_GX_CANONICAL_TEXGEN_SECTION_VERSION \
    ACGC_GX_CANONICAL_TEXGEN_STATE_VERSION
#define ACGC_GX_CANONICAL_TEXGEN_SECTION_MASK \
    ACGC_GX_CANONICAL_SECTION_MASK_TEXGENS
#define ACGC_GX_CANONICAL_TEXGEN_SECTION_BYTE_SIZE \
    ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE
#define ACGC_GX_CANONICAL_TEXGEN_SECTION_COUNT \
    ACGC_GX_CANONICAL_TEXGEN_STATE_COUNT
#define ACGC_GX_CANONICAL_TEXGEN_SECTION_CAPACITY \
    ACGC_GX_CANONICAL_TEXGEN_STATE_CAPACITY

#define ACGC_GX_CANONICAL_TEXGEN_COMPONENT_FUNCTION UINT32_C(0x01)
#define ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SOURCE UINT32_C(0x02)
#define ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ORDINARY_MATRIX UINT32_C(0x04)
#define ACGC_GX_CANONICAL_TEXGEN_COMPONENT_NORMALIZE UINT32_C(0x08)
#define ACGC_GX_CANONICAL_TEXGEN_COMPONENT_POST_MATRIX UINT32_C(0x10)
#define ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL UINT32_C(0x1F)

#define ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_TEXGEN UINT32_C(0x01)
#define ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_ORDINARY_MATRIX \
    UINT32_C(0x02)
#define ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_POST_MATRIX UINT32_C(0x04)
#define ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_SU UINT32_C(0x08)
#define ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_MASK UINT32_C(0x0F)

#define ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_MANUAL UINT32_C(0x01)
#define ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_SCALE_S UINT32_C(0x02)
#define ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_SCALE_T UINT32_C(0x04)
#define ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_BIAS_S UINT32_C(0x08)
#define ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_BIAS_T UINT32_C(0x10)
#define ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_CYLINDER_S UINT32_C(0x20)
#define ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_CYLINDER_T UINT32_C(0x40)
#define ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_ALL UINT32_C(0x7F)

/* Exact logical GX selector values; host slots and encoded fields are not valid. */
#define ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX3X4 UINT32_C(0)
#define ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX2X4 UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP0 UINT32_C(2)
#define ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP1 UINT32_C(3)
#define ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP2 UINT32_C(4)
#define ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP3 UINT32_C(5)
#define ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP4 UINT32_C(6)
#define ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP5 UINT32_C(7)
#define ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP6 UINT32_C(8)
#define ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP7 UINT32_C(9)
#define ACGC_GX_CANONICAL_TEXGEN_FUNCTION_SRTG UINT32_C(10)

#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_POS UINT32_C(0)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_NRM UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_BINRM UINT32_C(2)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TANGENT UINT32_C(3)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX0 UINT32_C(4)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX1 UINT32_C(5)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX2 UINT32_C(6)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX3 UINT32_C(7)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX4 UINT32_C(8)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX5 UINT32_C(9)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX6 UINT32_C(10)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX7 UINT32_C(11)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD0 UINT32_C(12)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD1 UINT32_C(13)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD2 UINT32_C(14)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD3 UINT32_C(15)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD4 UINT32_C(16)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD5 UINT32_C(17)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD6 UINT32_C(18)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR0 UINT32_C(19)
#define ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR1 UINT32_C(20)

#define ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4 UINT32_C(0)
#define ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX2X4 UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_2X4 UINT32_C(8)
#define ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_COUNT_3X4 UINT32_C(12)
#define ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_2X4 UINT32_C(0x000000FF)
#define ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4 UINT32_C(0x00000FFF)

#define ACGC_GX_CANONICAL_TEXGEN_ORDINARY_LOGICAL_ID(index) \
    ((uint32_t)(((index) < 10u) ? (30u + 3u * (index)) : 60u))
#define ACGC_GX_CANONICAL_TEXGEN_POST_LOGICAL_ID(index) \
    ((uint32_t)(((index) < 20u) ? (64u + 3u * (index)) : 125u))

typedef struct AcgcGxCanonicalTexgenHeader {
    uint32_t active_texgen_count;
    uint32_t texgen_capacity;
    uint32_t known_texgen_count;
    uint32_t ordinary_matrix_count;
    uint32_t ordinary_matrix_capacity;
    uint32_t post_matrix_count;
    uint32_t post_matrix_capacity;
    uint32_t su_count;
    uint32_t su_capacity;
    uint32_t texgen_known_mask;
    uint32_t ordinary_matrix_known_mask;
    uint32_t post_matrix_known_mask;
    uint32_t su_known_mask;
    uint32_t component_known_summary;
    uint32_t reserved[2];
} AcgcGxCanonicalTexgenHeader;

typedef struct AcgcGxCanonicalTexgenRecord {
    uint32_t function;
    uint32_t source;
    uint32_t ordinary_matrix_id;
    uint32_t normalize;
    uint32_t post_matrix_id;
    uint32_t component_known;
    uint32_t reserved[2];
} AcgcGxCanonicalTexgenRecord;

typedef struct AcgcGxCanonicalTexgenMatrixRecord {
    uint32_t logical_id;
    uint32_t last_load_type;
    uint32_t last_written_word_count;
    uint32_t known_word_mask;
    uint32_t words[12];
} AcgcGxCanonicalTexgenMatrixRecord;

typedef struct AcgcGxCanonicalTexgenSuRecord {
    uint32_t component_known;
    uint32_t manual_enable;
    uint32_t scale_s_raw_u16;
    uint32_t scale_t_raw_u16;
    uint32_t bias_s;
    uint32_t bias_t;
    uint32_t cylinder_s;
    uint32_t cylinder_t;
} AcgcGxCanonicalTexgenSuRecord;

typedef struct AcgcGxCanonicalTexgenState {
    AcgcGxCanonicalTexgenHeader header;
    AcgcGxCanonicalTexgenRecord texgen[
        ACGC_GX_CANONICAL_TEXGEN_CAPACITY];
    AcgcGxCanonicalTexgenMatrixRecord ordinary_matrix[
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_CAPACITY];
    AcgcGxCanonicalTexgenMatrixRecord post_matrix[
        ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_CAPACITY];
    AcgcGxCanonicalTexgenSuRecord su[ACGC_GX_CANONICAL_TEXGEN_SU_CAPACITY];
} AcgcGxCanonicalTexgenState;

/* Return nonzero only for a complete, strictly bounded value state. */
int acgc_gx_canonical_texgen_state_validate(
    const AcgcGxCanonicalTexgenState* state
);

/* Validate only the common-envelope directory metadata for section ID 4. */
int acgc_gx_canonical_texgen_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

/* Encode/decode exactly one 0xA40-byte section; failures do not mutate output. */
int acgc_gx_canonical_texgen_state_encode(
    const AcgcGxCanonicalTexgenState* state,
    uint8_t* destination,
    size_t destination_byte_size
);
int acgc_gx_canonical_texgen_state_decode(
    const uint8_t* source,
    size_t source_byte_size,
    AcgcGxCanonicalTexgenState* destination
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_TEXGEN_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_TEXGEN_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    sizeof(uint32_t) == 4,
    "canonical GX Texgen state requires 32-bit uint32_t"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalTexgenHeader) == ACGC_GX_CANONICAL_TEXGEN_HEADER_SIZE,
    "canonical GX Texgen header ABI size changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalTexgenRecord) ==
        ACGC_GX_CANONICAL_TEXGEN_RECORD_SIZE,
    "canonical GX Texgen record ABI size changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalTexgenMatrixRecord) ==
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_RECORD_SIZE,
    "canonical GX Texgen matrix ABI size changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalTexgenSuRecord) ==
        ACGC_GX_CANONICAL_TEXGEN_SU_RECORD_SIZE,
    "canonical GX Texgen SU ABI size changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalTexgenState) ==
        ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE,
    "canonical GX Texgen state ABI size changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    ACGC_GX_CANONICAL_TEXGEN_ALIGNOF(AcgcGxCanonicalTexgenState) ==
        ACGC_GX_CANONICAL_TEXGEN_STATE_ALIGNMENT,
    "canonical GX Texgen state ABI alignment changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTexgenState, header) == 0x000,
    "canonical GX Texgen header offset changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTexgenState, texgen) ==
        ACGC_GX_CANONICAL_TEXGEN_OFFSET,
    "canonical GX Texgen record offset changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTexgenState, ordinary_matrix) ==
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_OFFSET,
    "canonical GX ordinary matrix offset changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTexgenState, post_matrix) ==
        ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_OFFSET,
    "canonical GX post matrix offset changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTexgenState, su) ==
        ACGC_GX_CANONICAL_TEXGEN_SU_OFFSET,
    "canonical GX SU offset changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    sizeof(((AcgcGxCanonicalTexgenState*)0)->texgen[0]) == 0x20,
    "canonical GX Texgen record stride changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    sizeof(((AcgcGxCanonicalTexgenState*)0)->ordinary_matrix[0]) == 0x40,
    "canonical GX ordinary matrix stride changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    sizeof(((AcgcGxCanonicalTexgenState*)0)->post_matrix[0]) == 0x40,
    "canonical GX post matrix stride changed"
);
ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT(
    sizeof(((AcgcGxCanonicalTexgenState*)0)->su[0]) == 0x20,
    "canonical GX SU record stride changed"
);

#undef ACGC_GX_CANONICAL_TEXGEN_ALIGNOF
#undef ACGC_GX_CANONICAL_TEXGEN_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_TEXGEN_STATE_H */
