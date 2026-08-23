#ifndef ACGC_GX_CANONICAL_TRANSFORM_STATE_H
#define ACGC_GX_CANONICAL_TRANSFORM_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the renderer-neutral, value-only Canonical Transforms section for
 * the cumulative GX envelope. Its logical wire representation is 222
 * little-endian uint32 words in the field/record order below. The C ABI uses
 * fixed-width uint32_t members; a byte-stream owner must serialize those
 * words explicitly as little endian rather than memcpy-ing this struct on a
 * big-endian host.
 *
 * Position and normal record identity is implicit in the fixed slot number:
 * slot N names logical GX position/normal matrix ID N * 3. There is no
 * texture or post-texture matrix data in this section.
 *
 * The known mask permits a partial, non-renderable value state. Every field
 * whose bit is clear must be zero. A known current reference is stricter: it
 * must name one of the exact logical IDs and its matching position record
 * must also be known.
 */
#define ACGC_GX_CANONICAL_TRANSFORM_STATE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE UINT32_C(888)
#define ACGC_GX_CANONICAL_TRANSFORM_STATE_ALIGNMENT UINT32_C(4)
#define ACGC_GX_CANONICAL_TRANSFORM_STATE_COUNT UINT32_C(1)
#define ACGC_GX_CANONICAL_TRANSFORM_STATE_CAPACITY UINT32_C(1)

#define ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_WORD_COUNT UINT32_C(6)
#define ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT UINT32_C(10)
#define ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT UINT32_C(12)
#define ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT UINT32_C(9)
#define ACGC_GX_CANONICAL_TRANSFORM_RESERVED_WORD_COUNT UINT32_C(3)

#define ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_TYPE_OFFSET UINT32_C(0x000)
#define ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_OFFSET UINT32_C(0x004)
#define ACGC_GX_CANONICAL_TRANSFORM_KNOWN_MASK_OFFSET UINT32_C(0x01C)
#define ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_ID_OFFSET \
    UINT32_C(0x020)
#define ACGC_GX_CANONICAL_TRANSFORM_RESERVED_OFFSET UINT32_C(0x024)
#define ACGC_GX_CANONICAL_TRANSFORM_POSITION_OFFSET UINT32_C(0x030)
#define ACGC_GX_CANONICAL_TRANSFORM_NORMAL_OFFSET UINT32_C(0x210)
#define ACGC_GX_CANONICAL_TRANSFORM_END_OFFSET UINT32_C(0x378)

#define ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE UINT32_C(0)
#define ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_ORTHOGRAPHIC UINT32_C(1)

#define ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK UINT32_C(0x0001)
#define ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK \
    UINT32_C(0x0002)
#define ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(slot) \
    (UINT32_C(1) << (2u + (slot)))
#define ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(slot) \
    (UINT32_C(1) << (12u + (slot)))
#define ACGC_GX_CANONICAL_TRANSFORM_KNOWN_MASK UINT32_C(0x003FFFFF)
#define ACGC_GX_CANONICAL_TRANSFORM_RESERVED_MASK UINT32_C(0xFFC00000)

#define ACGC_GX_CANONICAL_TRANSFORM_LOGICAL_POSITION_ID_FIRST UINT32_C(0)
#define ACGC_GX_CANONICAL_TRANSFORM_LOGICAL_POSITION_ID_STRIDE UINT32_C(3)
#define ACGC_GX_CANONICAL_TRANSFORM_LOGICAL_POSITION_ID_LAST UINT32_C(27)

#define ACGC_GX_CANONICAL_TRANSFORM_SECTION_ID \
    ACGC_GX_CANONICAL_SECTION_ID_TRANSFORMS
#define ACGC_GX_CANONICAL_TRANSFORM_SECTION_VERSION \
    ACGC_GX_CANONICAL_TRANSFORM_STATE_VERSION
#define ACGC_GX_CANONICAL_TRANSFORM_SECTION_MASK \
    ACGC_GX_CANONICAL_SECTION_MASK_TRANSFORMS
#define ACGC_GX_CANONICAL_TRANSFORM_SECTION_BYTE_SIZE \
    ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE
#define ACGC_GX_CANONICAL_TRANSFORM_SECTION_COUNT \
    ACGC_GX_CANONICAL_TRANSFORM_STATE_COUNT
#define ACGC_GX_CANONICAL_TRANSFORM_SECTION_CAPACITY \
    ACGC_GX_CANONICAL_TRANSFORM_STATE_CAPACITY

typedef struct AcgcGxCanonicalTransformState {
    uint32_t projection_type;
    uint32_t projection[ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_WORD_COUNT];
    uint32_t known_mask;
    uint32_t current_position_id;
    uint32_t reserved[ACGC_GX_CANONICAL_TRANSFORM_RESERVED_WORD_COUNT];
    uint32_t position[
        ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT][
            ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT];
    uint32_t normal[
        ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT][
            ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT];
} AcgcGxCanonicalTransformState;

/* Return nonzero only for a canonical value state with exact zeroing rules. */
int acgc_gx_canonical_transform_state_validate(
    const AcgcGxCanonicalTransformState* state
);

/*
 * Validate the common envelope, then enforce the exact Transform directory
 * metadata when the section is present. An absent Transform entry is valid
 * only when its fixed section ID remains and all other directory words are
 * zero.
 */
int acgc_gx_canonical_transform_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

/* Encode exactly one fixed-size section; failures do not mutate output. */
int acgc_gx_canonical_transform_state_encode(
    const AcgcGxCanonicalTransformState* state,
    uint8_t* destination,
    size_t destination_byte_size
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_TRANSFORM_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_TRANSFORM_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    sizeof(uint32_t) == 4,
    "canonical GX Transform state requires 32-bit uint32_t"
);
ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    ACGC_GX_CANONICAL_TRANSFORM_STATE_ALIGNMENT == 4,
    "canonical GX Transform alignment contract changed"
);
ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalTransformState) ==
        ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE,
    "canonical GX Transform state ABI size changed"
);
ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    ACGC_GX_CANONICAL_TRANSFORM_ALIGNOF(AcgcGxCanonicalTransformState) ==
        ACGC_GX_CANONICAL_TRANSFORM_STATE_ALIGNMENT,
    "canonical GX Transform state ABI alignment changed"
);
ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTransformState, projection_type) ==
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_TYPE_OFFSET,
    "canonical GX Transform projection-type offset changed"
);
ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTransformState, projection) ==
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_OFFSET,
    "canonical GX Transform projection offset changed"
);
ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTransformState, known_mask) ==
        ACGC_GX_CANONICAL_TRANSFORM_KNOWN_MASK_OFFSET,
    "canonical GX Transform known-mask offset changed"
);
ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTransformState, current_position_id) ==
        ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_ID_OFFSET,
    "canonical GX Transform current-ID offset changed"
);
ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTransformState, reserved) ==
        ACGC_GX_CANONICAL_TRANSFORM_RESERVED_OFFSET,
    "canonical GX Transform reserved offset changed"
);
ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTransformState, position) ==
        ACGC_GX_CANONICAL_TRANSFORM_POSITION_OFFSET,
    "canonical GX Transform position offset changed"
);
ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTransformState, normal) ==
        ACGC_GX_CANONICAL_TRANSFORM_NORMAL_OFFSET,
    "canonical GX Transform normal offset changed"
);
ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    sizeof(((AcgcGxCanonicalTransformState*)0)->position[0]) ==
        ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT * 4,
    "canonical GX Transform position record size changed"
);
ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    sizeof(((AcgcGxCanonicalTransformState*)0)->normal[0]) ==
        ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT * 4,
    "canonical GX Transform normal record size changed"
);
ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT(
    ACGC_GX_CANONICAL_TRANSFORM_END_OFFSET ==
        sizeof(AcgcGxCanonicalTransformState),
    "canonical GX Transform end offset changed"
);

#undef ACGC_GX_CANONICAL_TRANSFORM_ALIGNOF
#undef ACGC_GX_CANONICAL_TRANSFORM_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_TRANSFORM_STATE_H */
