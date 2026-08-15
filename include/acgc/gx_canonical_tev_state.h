#ifndef ACGC_GX_CANONICAL_TEV_STATE_H
#define ACGC_GX_CANONICAL_TEV_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical GX TEV section 0x0020.  This is a value-only CPU ABI: the
 * logical byte stream is little-endian fixed-width words, and a byte-stream
 * owner must perform explicit little-endian conversion instead of copying
 * this host object as a wire image.  No field below is a pointer, host enum,
 * host float, native bool, or bit-field.
 *
 * The directory slot is section ID 6 and its stable section mask is 0x0020.
 * The four register records are ordered PREV, REG0, REG1, REG2; the four
 * KONST records and four swap-table records are ordered by their GX index.
 */
#define ACGC_GX_CANONICAL_TEV_STATE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_TEV_STATE_SIZE UINT32_C(2560)
#define ACGC_GX_CANONICAL_TEV_STATE_ALIGNMENT UINT32_C(4)

#define ACGC_GX_CANONICAL_TEV_HEADER_SIZE UINT32_C(64)
#define ACGC_GX_CANONICAL_TEV_STAGE_COUNT UINT32_C(16)
#define ACGC_GX_CANONICAL_TEV_STAGE_CAPACITY \
    ACGC_GX_CANONICAL_TEV_STAGE_COUNT
#define ACGC_GX_CANONICAL_TEV_STAGE_RECORD_SIZE UINT32_C(144)
#define ACGC_GX_CANONICAL_TEV_REGISTER_COUNT UINT32_C(4)
#define ACGC_GX_CANONICAL_TEV_REGISTER_RECORD_SIZE UINT32_C(16)
#define ACGC_GX_CANONICAL_TEV_KONST_COUNT UINT32_C(4)
#define ACGC_GX_CANONICAL_TEV_KONST_RECORD_SIZE UINT32_C(16)
#define ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT UINT32_C(4)
#define ACGC_GX_CANONICAL_TEV_SWAP_TABLE_RECORD_SIZE UINT32_C(16)

#define ACGC_GX_CANONICAL_TEV_STAGE_OFFSET \
    ACGC_GX_CANONICAL_TEV_HEADER_SIZE
#define ACGC_GX_CANONICAL_TEV_REGISTER_OFFSET \
    (ACGC_GX_CANONICAL_TEV_STAGE_OFFSET + \
     ACGC_GX_CANONICAL_TEV_STAGE_COUNT * \
         ACGC_GX_CANONICAL_TEV_STAGE_RECORD_SIZE)
#define ACGC_GX_CANONICAL_TEV_KONST_OFFSET \
    (ACGC_GX_CANONICAL_TEV_REGISTER_OFFSET + \
     ACGC_GX_CANONICAL_TEV_REGISTER_COUNT * \
         ACGC_GX_CANONICAL_TEV_REGISTER_RECORD_SIZE)
#define ACGC_GX_CANONICAL_TEV_SWAP_TABLE_OFFSET \
    (ACGC_GX_CANONICAL_TEV_KONST_OFFSET + \
     ACGC_GX_CANONICAL_TEV_KONST_COUNT * \
         ACGC_GX_CANONICAL_TEV_KONST_RECORD_SIZE)

#define ACGC_GX_CANONICAL_TEV_ACTIVE_STAGE_COUNT_MIN UINT32_C(1)
#define ACGC_GX_CANONICAL_TEV_ACTIVE_STAGE_COUNT_MAX \
    ACGC_GX_CANONICAL_TEV_STAGE_CAPACITY
#define ACGC_GX_CANONICAL_TEV_COMPONENT_VALID_MASK UINT32_C(0x0000000F)

#define ACGC_GX_CANONICAL_TEV_SECTION_ID \
    ACGC_GX_CANONICAL_SECTION_ID_TEV
#define ACGC_GX_CANONICAL_TEV_SECTION_VERSION \
    ACGC_GX_CANONICAL_TEV_STATE_VERSION
#define ACGC_GX_CANONICAL_TEV_SECTION_MASK \
    ACGC_GX_CANONICAL_SECTION_MASK_TEV
#define ACGC_GX_CANONICAL_TEV_SECTION_BYTE_SIZE \
    ACGC_GX_CANONICAL_TEV_STATE_SIZE
#define ACGC_GX_CANONICAL_TEV_SECTION_COUNT_MIN \
    ACGC_GX_CANONICAL_TEV_ACTIVE_STAGE_COUNT_MIN
#define ACGC_GX_CANONICAL_TEV_SECTION_COUNT_MAX \
    ACGC_GX_CANONICAL_TEV_ACTIVE_STAGE_COUNT_MAX
#define ACGC_GX_CANONICAL_TEV_SECTION_CAPACITY \
    ACGC_GX_CANONICAL_TEV_STAGE_CAPACITY

#define ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_COLOR_INPUT_MAX UINT32_C(15)
#define ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_ALPHA_INPUT_MAX UINT32_C(7)
#define ACGC_GX_CANONICAL_TEV_OPERATION_ADD UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_OPERATION_SUB UINT32_C(1)
#define ACGC_GX_CANONICAL_TEV_OPERATION_COMPARE_MIN UINT32_C(8)
#define ACGC_GX_CANONICAL_TEV_OPERATION_COMPARE_MAX UINT32_C(15)
#define ACGC_GX_CANONICAL_TEV_BIAS_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_BIAS_MAX UINT32_C(2)
#define ACGC_GX_CANONICAL_TEV_SCALE_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_SCALE_MAX UINT32_C(3)
#define ACGC_GX_CANONICAL_TEV_BOOLEAN_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_BOOLEAN_MAX UINT32_C(1)
#define ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_REGISTER_INDEX_MAX UINT32_C(3)

#define ACGC_GX_CANONICAL_TEV_TEXCOORD_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_TEXCOORD_MAX UINT32_C(7)
#define ACGC_GX_CANONICAL_TEV_TEXCOORD_NULL UINT32_C(0x000000FF)
#define ACGC_GX_CANONICAL_TEV_TEXMAP_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_TEXMAP_MAX UINT32_C(7)
#define ACGC_GX_CANONICAL_TEV_TEXMAP_NULL UINT32_C(0x000000FF)
#define ACGC_GX_CANONICAL_TEV_TEXMAP_DISABLE UINT32_C(0x00000100)
#define ACGC_GX_CANONICAL_TEV_CHANNEL_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_CHANNEL_MAX UINT32_C(8)
#define ACGC_GX_CANONICAL_TEV_CHANNEL_NULL UINT32_C(0x000000FF)

#define ACGC_GX_CANONICAL_TEV_KCOLOR_SELECTOR_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_KCOLOR_SELECTOR_LOW_MAX UINT32_C(7)
#define ACGC_GX_CANONICAL_TEV_KCOLOR_SELECTOR_HIGH_MIN UINT32_C(12)
#define ACGC_GX_CANONICAL_TEV_KCOLOR_SELECTOR_MAX UINT32_C(31)
#define ACGC_GX_CANONICAL_TEV_KALPHA_SELECTOR_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_KALPHA_SELECTOR_LOW_MAX UINT32_C(7)
#define ACGC_GX_CANONICAL_TEV_KALPHA_SELECTOR_HIGH_MIN UINT32_C(16)
#define ACGC_GX_CANONICAL_TEV_KALPHA_SELECTOR_MAX UINT32_C(31)
#define ACGC_GX_CANONICAL_TEV_SWAP_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_SWAP_MAX UINT32_C(3)

#define ACGC_GX_CANONICAL_TEV_INDIRECT_STAGE_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_STAGE_MAX UINT32_C(3)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_FORMAT_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_FORMAT_MAX UINT32_C(3)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_BIAS_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_BIAS_MAX UINT32_C(7)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_ALPHA_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_ALPHA_MAX UINT32_C(3)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_WRAP_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_WRAP_MAX UINT32_C(6)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_OFF UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_0 UINT32_C(1)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_1 UINT32_C(2)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_2 UINT32_C(3)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S0 UINT32_C(5)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S1 UINT32_C(6)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S2 UINT32_C(7)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T0 UINT32_C(9)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T1 UINT32_C(10)
#define ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T2 UINT32_C(11)

#define ACGC_GX_CANONICAL_TEV_REGISTER_COMPONENT_MIN INT32_C(-1024)
#define ACGC_GX_CANONICAL_TEV_REGISTER_COMPONENT_MAX INT32_C(1023)
#define ACGC_GX_CANONICAL_TEV_KONST_COMPONENT_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_TEV_KONST_COMPONENT_MAX UINT32_C(255)

typedef struct AcgcGxCanonicalTevHeader {
    uint32_t version;
    uint32_t section_id;
    uint32_t section_mask;
    uint32_t byte_size;
    uint32_t active_stage_count;
    uint32_t stage_capacity;
    uint32_t component_valid_mask;
    uint32_t reserved;
    uint32_t stage_offset;
    uint32_t stage_record_size;
    uint32_t register_offset;
    uint32_t register_record_size;
    uint32_t konst_offset;
    uint32_t konst_record_size;
    uint32_t swap_table_offset;
    uint32_t swap_table_record_size;
} AcgcGxCanonicalTevHeader;

/* One 144-byte record in logical stage order 0 through 15. */
typedef struct AcgcGxCanonicalTevStage {
    uint32_t color_a;
    uint32_t color_b;
    uint32_t color_c;
    uint32_t color_d;
    uint32_t alpha_a;
    uint32_t alpha_b;
    uint32_t alpha_c;
    uint32_t alpha_d;
    uint32_t color_op;
    uint32_t color_bias;
    uint32_t color_scale;
    uint32_t color_clamp;
    uint32_t color_out;
    uint32_t alpha_op;
    uint32_t alpha_bias;
    uint32_t alpha_scale;
    uint32_t alpha_clamp;
    uint32_t alpha_out;
    uint32_t tex_coord;
    uint32_t tex_map;
    uint32_t color_chan;
    uint32_t k_color_sel;
    uint32_t k_alpha_sel;
    uint32_t ras_swap;
    uint32_t tex_swap;
    uint32_t ind_stage;
    uint32_t ind_format;
    uint32_t ind_bias;
    uint32_t ind_mtx;
    uint32_t ind_wrap_s;
    uint32_t ind_wrap_t;
    uint32_t ind_add_prev;
    uint32_t ind_lod;
    uint32_t ind_alpha;
    uint32_t reserved[2];
} AcgcGxCanonicalTevStage;

/* Exact signed S10 register values, ordered R/G/B/A. */
typedef struct AcgcGxCanonicalTevRegister {
    int32_t r;
    int32_t g;
    int32_t b;
    int32_t a;
} AcgcGxCanonicalTevRegister;

/* Exact widened GX u8 KONST values, ordered R/G/B/A. */
typedef struct AcgcGxCanonicalTevKonst {
    uint32_t r;
    uint32_t g;
    uint32_t b;
    uint32_t a;
} AcgcGxCanonicalTevKonst;

/* Exact GX channel selectors in a swap table, ordered R/G/B/A. */
typedef struct AcgcGxCanonicalTevSwapTable {
    uint32_t r;
    uint32_t g;
    uint32_t b;
    uint32_t a;
} AcgcGxCanonicalTevSwapTable;

typedef struct AcgcGxCanonicalTevState {
    AcgcGxCanonicalTevHeader header;
    AcgcGxCanonicalTevStage stages[ACGC_GX_CANONICAL_TEV_STAGE_COUNT];
    AcgcGxCanonicalTevRegister registers[
        ACGC_GX_CANONICAL_TEV_REGISTER_COUNT];
    AcgcGxCanonicalTevKonst konst[ACGC_GX_CANONICAL_TEV_KONST_COUNT];
    AcgcGxCanonicalTevSwapTable swap_tables[
        ACGC_GX_CANONICAL_TEV_SWAP_TABLE_COUNT];
} AcgcGxCanonicalTevState;

/* Return nonzero only for a complete, strictly bounded TEV value section. */
int acgc_gx_canonical_tev_state_validate(
    const AcgcGxCanonicalTevState* state
);

/*
 * Validate the unchanged common envelope first, then enforce the exact TEV
 * directory metadata.  An absent TEV entry is valid only when the common
 * fixed directory slot remains identified as TEV and every other metadata
 * word in that entry is zero.
 */
int acgc_gx_canonical_tev_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_TEV_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_TEV_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_TEV_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_TEV_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    sizeof(uint32_t) == 4 && sizeof(int32_t) == 4,
    "canonical GX TEV requires 32-bit fixed-width integers"
);
ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    ACGC_GX_CANONICAL_TEV_STATE_ALIGNMENT == 4,
    "canonical GX TEV alignment contract changed"
);
ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalTevHeader) ==
        ACGC_GX_CANONICAL_TEV_HEADER_SIZE,
    "canonical GX TEV header ABI size changed"
);
ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    ACGC_GX_CANONICAL_TEV_ALIGNOF(AcgcGxCanonicalTevHeader) ==
        ACGC_GX_CANONICAL_TEV_STATE_ALIGNMENT,
    "canonical GX TEV header alignment changed"
);
ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTevHeader, version) == 0 &&
        offsetof(AcgcGxCanonicalTevHeader, section_id) == 4 &&
        offsetof(AcgcGxCanonicalTevHeader, section_mask) == 8 &&
        offsetof(AcgcGxCanonicalTevHeader, byte_size) == 12 &&
        offsetof(AcgcGxCanonicalTevHeader, active_stage_count) == 16 &&
        offsetof(AcgcGxCanonicalTevHeader, stage_capacity) == 20 &&
        offsetof(AcgcGxCanonicalTevHeader, component_valid_mask) == 24 &&
        offsetof(AcgcGxCanonicalTevHeader, reserved) == 28 &&
        offsetof(AcgcGxCanonicalTevHeader, stage_offset) == 32 &&
        offsetof(AcgcGxCanonicalTevHeader, stage_record_size) == 36 &&
        offsetof(AcgcGxCanonicalTevHeader, register_offset) == 40 &&
        offsetof(AcgcGxCanonicalTevHeader, register_record_size) == 44 &&
        offsetof(AcgcGxCanonicalTevHeader, konst_offset) == 48 &&
        offsetof(AcgcGxCanonicalTevHeader, konst_record_size) == 52 &&
        offsetof(AcgcGxCanonicalTevHeader, swap_table_offset) == 56 &&
        offsetof(AcgcGxCanonicalTevHeader, swap_table_record_size) == 60,
    "canonical GX TEV header offsets changed"
);
ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalTevStage) ==
        ACGC_GX_CANONICAL_TEV_STAGE_RECORD_SIZE,
    "canonical GX TEV stage ABI size changed"
);
ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    ACGC_GX_CANONICAL_TEV_ALIGNOF(AcgcGxCanonicalTevStage) ==
        ACGC_GX_CANONICAL_TEV_STATE_ALIGNMENT,
    "canonical GX TEV stage alignment changed"
);
ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTevStage, color_a) == 0 &&
        offsetof(AcgcGxCanonicalTevStage, alpha_a) == 16 &&
        offsetof(AcgcGxCanonicalTevStage, color_op) == 32 &&
        offsetof(AcgcGxCanonicalTevStage, alpha_op) == 52 &&
        offsetof(AcgcGxCanonicalTevStage, tex_coord) == 72 &&
        offsetof(AcgcGxCanonicalTevStage, k_color_sel) == 84 &&
        offsetof(AcgcGxCanonicalTevStage, ras_swap) == 92 &&
        offsetof(AcgcGxCanonicalTevStage, ind_stage) == 100 &&
        offsetof(AcgcGxCanonicalTevStage, ind_alpha) == 132 &&
        offsetof(AcgcGxCanonicalTevStage, reserved) == 136,
    "canonical GX TEV stage offsets changed"
);
ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalTevRegister) ==
        ACGC_GX_CANONICAL_TEV_REGISTER_RECORD_SIZE &&
        sizeof(AcgcGxCanonicalTevKonst) ==
            ACGC_GX_CANONICAL_TEV_KONST_RECORD_SIZE &&
        sizeof(AcgcGxCanonicalTevSwapTable) ==
            ACGC_GX_CANONICAL_TEV_SWAP_TABLE_RECORD_SIZE,
    "canonical GX TEV fixed record size changed"
);
ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    ACGC_GX_CANONICAL_TEV_ALIGNOF(AcgcGxCanonicalTevRegister) ==
            ACGC_GX_CANONICAL_TEV_STATE_ALIGNMENT &&
        ACGC_GX_CANONICAL_TEV_ALIGNOF(AcgcGxCanonicalTevKonst) ==
            ACGC_GX_CANONICAL_TEV_STATE_ALIGNMENT &&
        ACGC_GX_CANONICAL_TEV_ALIGNOF(AcgcGxCanonicalTevSwapTable) ==
            ACGC_GX_CANONICAL_TEV_STATE_ALIGNMENT,
    "canonical GX TEV fixed record alignment changed"
);
ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTevRegister, r) == 0 &&
        offsetof(AcgcGxCanonicalTevRegister, a) == 12 &&
        offsetof(AcgcGxCanonicalTevKonst, r) == 0 &&
        offsetof(AcgcGxCanonicalTevKonst, a) == 12 &&
        offsetof(AcgcGxCanonicalTevSwapTable, r) == 0 &&
        offsetof(AcgcGxCanonicalTevSwapTable, a) == 12,
    "canonical GX TEV fixed record offsets changed"
);
ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalTevState) == ACGC_GX_CANONICAL_TEV_STATE_SIZE,
    "canonical GX TEV state ABI size changed"
);
ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    ACGC_GX_CANONICAL_TEV_ALIGNOF(AcgcGxCanonicalTevState) ==
        ACGC_GX_CANONICAL_TEV_STATE_ALIGNMENT,
    "canonical GX TEV state alignment changed"
);
ACGC_GX_CANONICAL_TEV_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTevState, header) == 0 &&
        offsetof(AcgcGxCanonicalTevState, stages) ==
            ACGC_GX_CANONICAL_TEV_STAGE_OFFSET &&
        offsetof(AcgcGxCanonicalTevState, registers) ==
            ACGC_GX_CANONICAL_TEV_REGISTER_OFFSET &&
        offsetof(AcgcGxCanonicalTevState, konst) ==
            ACGC_GX_CANONICAL_TEV_KONST_OFFSET &&
        offsetof(AcgcGxCanonicalTevState, swap_tables) ==
            ACGC_GX_CANONICAL_TEV_SWAP_TABLE_OFFSET,
    "canonical GX TEV state offsets changed"
);

#undef ACGC_GX_CANONICAL_TEV_ALIGNOF
#undef ACGC_GX_CANONICAL_TEV_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_TEV_STATE_H */
