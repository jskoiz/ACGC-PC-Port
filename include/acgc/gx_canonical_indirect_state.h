#ifndef ACGC_GX_CANONICAL_INDIRECT_STATE_H
#define ACGC_GX_CANONICAL_INDIRECT_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_geometry_state.h"
#include "acgc/gx_canonical_state.h"
#include "acgc/gx_canonical_tev_state.h"
#include "acgc/gx_canonical_texture_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical GX Indirect section 0x1000.  This is a value-only, pointer-free
 * CPU ABI: every logical field is one fixed-width little-endian word.  The
 * C structs below describe the decoded words; a byte-stream owner must
 * explicitly convert each word to and from little-endian.
 *
 * The section has a fourteen-word header, four six-word order records, and
 * three eight-word matrix records.  Order records are in indirect stage
 * order 0..3. Matrix coefficients are interleaved as S0,T0,S1,T1,S2,T2,
 * matching the guest GXSetIndTexMtx 2-by-3 matrix columns. encoded_scale is
 * the six-bit packed exponent after the guest's +0x11 encoding; its register
 * representation is split into three two-bit chunks by GX.
 *
 * Inactive order records and invalid matrix records are canonical zero
 * records. Matrix validity is explicit so an unreferenced matrix can remain
 * absent without inventing a host pointer or float representation.
 * Per-TEV indirect fields are intentionally not repeated here; they remain
 * solely owned by AcgcGxCanonicalTevState.
 */
#define ACGC_GX_CANONICAL_INDIRECT_STATE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE UINT32_C(248)
#define ACGC_GX_CANONICAL_INDIRECT_STATE_ALIGNMENT UINT32_C(4)

#define ACGC_GX_CANONICAL_INDIRECT_HEADER_WORD_COUNT UINT32_C(14)
#define ACGC_GX_CANONICAL_INDIRECT_HEADER_SIZE UINT32_C(56)

#define ACGC_GX_CANONICAL_INDIRECT_ORDER_COUNT UINT32_C(4)
#define ACGC_GX_CANONICAL_INDIRECT_ORDER_CAPACITY \
    ACGC_GX_CANONICAL_INDIRECT_ORDER_COUNT
#define ACGC_GX_CANONICAL_INDIRECT_ORDER_WORD_COUNT UINT32_C(6)
#define ACGC_GX_CANONICAL_INDIRECT_ORDER_RECORD_SIZE UINT32_C(24)
#define ACGC_GX_CANONICAL_INDIRECT_ORDER_OFFSET \
    ACGC_GX_CANONICAL_INDIRECT_HEADER_SIZE

#define ACGC_GX_CANONICAL_INDIRECT_MATRIX_COUNT UINT32_C(3)
#define ACGC_GX_CANONICAL_INDIRECT_MATRIX_CAPACITY \
    ACGC_GX_CANONICAL_INDIRECT_MATRIX_COUNT
#define ACGC_GX_CANONICAL_INDIRECT_MATRIX_WORD_COUNT UINT32_C(8)
#define ACGC_GX_CANONICAL_INDIRECT_MATRIX_RECORD_SIZE UINT32_C(32)
#define ACGC_GX_CANONICAL_INDIRECT_MATRIX_OFFSET \
    (ACGC_GX_CANONICAL_INDIRECT_ORDER_OFFSET + \
     ACGC_GX_CANONICAL_INDIRECT_ORDER_COUNT * \
         ACGC_GX_CANONICAL_INDIRECT_ORDER_RECORD_SIZE)

#define ACGC_GX_CANONICAL_INDIRECT_ACTIVE_STAGE_COUNT_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_INDIRECT_ACTIVE_STAGE_COUNT_MAX \
    ACGC_GX_CANONICAL_INDIRECT_ORDER_CAPACITY
#define ACGC_GX_CANONICAL_INDIRECT_ACTIVE_ORDER_MASK UINT32_C(0x0000000F)
#define ACGC_GX_CANONICAL_INDIRECT_MATRIX_VALID_MASK UINT32_C(0x00000007)

#define ACGC_GX_CANONICAL_INDIRECT_TEXCOORD_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_INDIRECT_TEXCOORD_MAX UINT32_C(7)
#define ACGC_GX_CANONICAL_INDIRECT_TEXMAP_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_INDIRECT_TEXMAP_MAX UINT32_C(7)
#define ACGC_GX_CANONICAL_INDIRECT_SCALE_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_INDIRECT_SCALE_MAX UINT32_C(8)
#define ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MIN INT32_C(-1024)
#define ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MAX INT32_C(1023)
#define ACGC_GX_CANONICAL_INDIRECT_ENCODED_SCALE_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_INDIRECT_ENCODED_SCALE_MAX UINT32_C(63)

#define ACGC_GX_CANONICAL_INDIRECT_SECTION_ID \
    ACGC_GX_CANONICAL_SECTION_ID_INDIRECT
#define ACGC_GX_CANONICAL_INDIRECT_SECTION_VERSION \
    ACGC_GX_CANONICAL_INDIRECT_STATE_VERSION
#define ACGC_GX_CANONICAL_INDIRECT_SECTION_MASK \
    ACGC_GX_CANONICAL_SECTION_MASK_INDIRECT
#define ACGC_GX_CANONICAL_INDIRECT_SECTION_BYTE_SIZE \
    ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE
#define ACGC_GX_CANONICAL_INDIRECT_SECTION_COUNT UINT32_C(1)
#define ACGC_GX_CANONICAL_INDIRECT_SECTION_CAPACITY UINT32_C(1)

typedef struct AcgcGxCanonicalIndirectHeader {
    uint32_t version;
    uint32_t section_id;
    uint32_t section_mask;
    uint32_t byte_size;
    uint32_t active_indirect_stage_count;
    uint32_t order_capacity;
    uint32_t order_record_size;
    uint32_t order_offset;
    uint32_t active_order_mask;
    uint32_t matrix_capacity;
    uint32_t matrix_record_size;
    uint32_t matrix_offset;
    uint32_t matrix_valid_mask;
    uint32_t reserved;
} AcgcGxCanonicalIndirectHeader;

typedef struct AcgcGxCanonicalIndirectOrder {
    uint32_t tex_coord;
    uint32_t tex_map;
    uint32_t scale_s;
    uint32_t scale_t;
    uint32_t reserved[2];
} AcgcGxCanonicalIndirectOrder;

typedef struct AcgcGxCanonicalIndirectMatrix {
    int32_t s0;
    int32_t t0;
    int32_t s1;
    int32_t t1;
    int32_t s2;
    int32_t t2;
    uint32_t encoded_scale;
    uint32_t reserved;
} AcgcGxCanonicalIndirectMatrix;

typedef struct AcgcGxCanonicalIndirectState {
    AcgcGxCanonicalIndirectHeader header;
    AcgcGxCanonicalIndirectOrder orders[
        ACGC_GX_CANONICAL_INDIRECT_ORDER_CAPACITY];
    AcgcGxCanonicalIndirectMatrix matrices[
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_CAPACITY];
} AcgcGxCanonicalIndirectState;

/* Return nonzero only for a complete, strictly bounded Indirect value. */
int acgc_gx_canonical_indirect_state_validate(
    const AcgcGxCanonicalIndirectState* state
);

/* Validate only section-directory metadata in the common envelope. */
int acgc_gx_canonical_indirect_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

/*
 * Cross-validate Indirect with canonical TEV ownership and optional
 * Texgen/Texture dependency results. The optional contexts are checked when
 * supplied; NULL means that particular dependency is unavailable at this
 * boundary. The TEV object is required and is passed by reference so its
 * per-stage Indirect fields cannot be duplicated in this section.
 */
int acgc_gx_canonical_indirect_state_validate_dependencies(
    const AcgcGxCanonicalIndirectState* state,
    const AcgcGxCanonicalTevState* tev,
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalGeometryDependencyResults* geometry_dependencies
);

/* Encode exactly one 0xF8-byte section; failures do not mutate output. */
int acgc_gx_canonical_indirect_state_encode(
    const AcgcGxCanonicalIndirectState* state,
    uint8_t* destination,
    size_t destination_byte_size
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_INDIRECT_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_INDIRECT_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_INDIRECT_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_INDIRECT_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_INDIRECT_STATIC_ASSERT(
    sizeof(uint32_t) == 4 && sizeof(int32_t) == 4,
    "canonical GX Indirect requires 32-bit fixed-width integers"
);
ACGC_GX_CANONICAL_INDIRECT_STATIC_ASSERT(
    ACGC_GX_CANONICAL_INDIRECT_STATE_ALIGNMENT == 4,
    "canonical GX Indirect alignment contract changed"
);
ACGC_GX_CANONICAL_INDIRECT_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalIndirectHeader) ==
        ACGC_GX_CANONICAL_INDIRECT_HEADER_SIZE &&
        ACGC_GX_CANONICAL_INDIRECT_ALIGNOF(
            AcgcGxCanonicalIndirectHeader) ==
            ACGC_GX_CANONICAL_INDIRECT_STATE_ALIGNMENT,
    "canonical GX Indirect header ABI changed"
);
ACGC_GX_CANONICAL_INDIRECT_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalIndirectHeader, version) == 0 &&
        offsetof(AcgcGxCanonicalIndirectHeader, section_id) == 4 &&
        offsetof(AcgcGxCanonicalIndirectHeader, section_mask) == 8 &&
        offsetof(AcgcGxCanonicalIndirectHeader, byte_size) == 12 &&
        offsetof(AcgcGxCanonicalIndirectHeader,
            active_indirect_stage_count) == 16 &&
        offsetof(AcgcGxCanonicalIndirectHeader, order_capacity) == 20 &&
        offsetof(AcgcGxCanonicalIndirectHeader, order_record_size) == 24 &&
        offsetof(AcgcGxCanonicalIndirectHeader, order_offset) == 28 &&
        offsetof(AcgcGxCanonicalIndirectHeader, active_order_mask) == 32 &&
        offsetof(AcgcGxCanonicalIndirectHeader, matrix_capacity) == 36 &&
        offsetof(AcgcGxCanonicalIndirectHeader, matrix_record_size) == 40 &&
        offsetof(AcgcGxCanonicalIndirectHeader, matrix_offset) == 44 &&
        offsetof(AcgcGxCanonicalIndirectHeader, matrix_valid_mask) == 48 &&
        offsetof(AcgcGxCanonicalIndirectHeader, reserved) == 52,
    "canonical GX Indirect header offsets changed"
);
ACGC_GX_CANONICAL_INDIRECT_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalIndirectOrder) ==
        ACGC_GX_CANONICAL_INDIRECT_ORDER_RECORD_SIZE &&
        ACGC_GX_CANONICAL_INDIRECT_ALIGNOF(
            AcgcGxCanonicalIndirectOrder) ==
            ACGC_GX_CANONICAL_INDIRECT_STATE_ALIGNMENT,
    "canonical GX Indirect order ABI changed"
);
ACGC_GX_CANONICAL_INDIRECT_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalIndirectOrder, tex_coord) == 0 &&
        offsetof(AcgcGxCanonicalIndirectOrder, tex_map) == 4 &&
        offsetof(AcgcGxCanonicalIndirectOrder, scale_s) == 8 &&
        offsetof(AcgcGxCanonicalIndirectOrder, scale_t) == 12 &&
        offsetof(AcgcGxCanonicalIndirectOrder, reserved) == 16,
    "canonical GX Indirect order offsets changed"
);
ACGC_GX_CANONICAL_INDIRECT_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalIndirectMatrix) ==
        ACGC_GX_CANONICAL_INDIRECT_MATRIX_RECORD_SIZE &&
        ACGC_GX_CANONICAL_INDIRECT_ALIGNOF(
            AcgcGxCanonicalIndirectMatrix) ==
            ACGC_GX_CANONICAL_INDIRECT_STATE_ALIGNMENT,
    "canonical GX Indirect matrix ABI changed"
);
ACGC_GX_CANONICAL_INDIRECT_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalIndirectMatrix, s0) == 0 &&
        offsetof(AcgcGxCanonicalIndirectMatrix, t0) == 4 &&
        offsetof(AcgcGxCanonicalIndirectMatrix, s1) == 8 &&
        offsetof(AcgcGxCanonicalIndirectMatrix, t1) == 12 &&
        offsetof(AcgcGxCanonicalIndirectMatrix, s2) == 16 &&
        offsetof(AcgcGxCanonicalIndirectMatrix, t2) == 20 &&
        offsetof(AcgcGxCanonicalIndirectMatrix, encoded_scale) == 24 &&
        offsetof(AcgcGxCanonicalIndirectMatrix, reserved) == 28,
    "canonical GX Indirect matrix offsets changed"
);
ACGC_GX_CANONICAL_INDIRECT_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalIndirectState) ==
        ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE &&
        ACGC_GX_CANONICAL_INDIRECT_ALIGNOF(
            AcgcGxCanonicalIndirectState) ==
            ACGC_GX_CANONICAL_INDIRECT_STATE_ALIGNMENT,
    "canonical GX Indirect state ABI changed"
);
ACGC_GX_CANONICAL_INDIRECT_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalIndirectState, header) == 0 &&
        offsetof(AcgcGxCanonicalIndirectState, orders) ==
            ACGC_GX_CANONICAL_INDIRECT_ORDER_OFFSET &&
        offsetof(AcgcGxCanonicalIndirectState, matrices) ==
            ACGC_GX_CANONICAL_INDIRECT_MATRIX_OFFSET,
    "canonical GX Indirect state offsets changed"
);

#undef ACGC_GX_CANONICAL_INDIRECT_ALIGNOF
#undef ACGC_GX_CANONICAL_INDIRECT_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_INDIRECT_STATE_H */
