#ifndef ACGC_GX_CANONICAL_LIGHTING_STATE_H
#define ACGC_GX_CANONICAL_LIGHTING_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the renderer-neutral, value-only Lighting section for the
 * cumulative canonical GX envelope. The logical wire representation is 129
 * little-endian uint32 words in field order. The C ABI uses fixed-width
 * uint32_t members; a byte-stream owner must perform explicit little-endian
 * conversion rather than memcpy-ing this struct on a big-endian host.
 *
 * color_rgba8 is logical RGBA8: R occupies bits 0..7, G 8..15, B 16..23,
 * and A 24..31. The twelve following words are IEEE-754 binary32 bit
 * patterns for angular attenuation, distance attenuation, position, and
 * direction. A loaded slot requires all twelve words to be finite. A zero
 * direction is valid and directions are not normalized at this boundary.
 * An all-zero state is an explicit empty state; unknown producer provenance
 * has no in-band sentinel and must remain absent or fail closed upstream.
 *
 * The record follows the final 16-word GX light register value order, but it
 * is not a GXLightObj or host-struct serialization. The three reserved words
 * are required to be zero; constructor enums, pointers, and object indices
 * are not represented here.
 */
#define ACGC_GX_CANONICAL_LIGHTING_STATE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_LIGHTING_STATE_SIZE UINT32_C(516)
#define ACGC_GX_CANONICAL_LIGHTING_STATE_ALIGNMENT UINT32_C(4)
#define ACGC_GX_CANONICAL_LIGHTING_STATE_COUNT UINT32_C(8)
#define ACGC_GX_CANONICAL_LIGHTING_STATE_CAPACITY UINT32_C(8)
#define ACGC_GX_CANONICAL_LIGHTING_SLOT_COUNT UINT32_C(8)
#define ACGC_GX_CANONICAL_LIGHTING_RECORD_SIZE UINT32_C(64)
#define ACGC_GX_CANONICAL_LIGHTING_RESERVED_WORD_COUNT UINT32_C(3)
#define ACGC_GX_CANONICAL_LIGHTING_FLOAT_VECTOR_COUNT UINT32_C(3)

#define ACGC_GX_CANONICAL_LIGHTING_LOADED_MASK_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_LIGHTING_LOADED_MASK_MAX UINT32_C(0x000000FF)

#define ACGC_GX_CANONICAL_LIGHTING_SECTION_ID \
    ACGC_GX_CANONICAL_SECTION_ID_LIGHTING
#define ACGC_GX_CANONICAL_LIGHTING_SECTION_VERSION \
    ACGC_GX_CANONICAL_LIGHTING_STATE_VERSION
#define ACGC_GX_CANONICAL_LIGHTING_SECTION_MASK \
    ACGC_GX_CANONICAL_SECTION_MASK_LIGHTING
#define ACGC_GX_CANONICAL_LIGHTING_SECTION_BYTE_SIZE \
    ACGC_GX_CANONICAL_LIGHTING_STATE_SIZE
#define ACGC_GX_CANONICAL_LIGHTING_SECTION_COUNT \
    ACGC_GX_CANONICAL_LIGHTING_STATE_COUNT
#define ACGC_GX_CANONICAL_LIGHTING_SECTION_CAPACITY \
    ACGC_GX_CANONICAL_LIGHTING_STATE_CAPACITY

typedef struct AcgcGxCanonicalLightingRecord {
    uint32_t reserved[ACGC_GX_CANONICAL_LIGHTING_RESERVED_WORD_COUNT];
    uint32_t color_rgba8;
    uint32_t angular_attenuation[
        ACGC_GX_CANONICAL_LIGHTING_FLOAT_VECTOR_COUNT];
    uint32_t distance_attenuation[
        ACGC_GX_CANONICAL_LIGHTING_FLOAT_VECTOR_COUNT];
    uint32_t position[ACGC_GX_CANONICAL_LIGHTING_FLOAT_VECTOR_COUNT];
    uint32_t direction[ACGC_GX_CANONICAL_LIGHTING_FLOAT_VECTOR_COUNT];
} AcgcGxCanonicalLightingRecord;

typedef struct AcgcGxCanonicalLightingState {
    uint32_t loaded_mask;
    AcgcGxCanonicalLightingRecord records[
        ACGC_GX_CANONICAL_LIGHTING_SLOT_COUNT];
} AcgcGxCanonicalLightingState;

/* Return nonzero only for a complete, structurally valid Lighting value. */
int acgc_gx_canonical_lighting_state_validate(
    const AcgcGxCanonicalLightingState* state
);

/*
 * Validate the common envelope, then enforce the exact Lighting entry
 * metadata when the section is present. An absent entry is valid only when
 * its fixed section ID remains and all other directory metadata words are
 * zero. This helper validates metadata only; it does not invent a producer or
 * inspect payload bytes.
 */
int acgc_gx_canonical_lighting_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_LIGHTING_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_LIGHTING_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    sizeof(uint32_t) == 4,
    "canonical GX Lighting state requires 32-bit uint32_t"
);
ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    ACGC_GX_CANONICAL_LIGHTING_STATE_ALIGNMENT == 4,
    "canonical GX Lighting alignment contract changed"
);
ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalLightingRecord) ==
        ACGC_GX_CANONICAL_LIGHTING_RECORD_SIZE,
    "canonical GX Lighting record ABI size changed"
);
ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    ACGC_GX_CANONICAL_LIGHTING_ALIGNOF(AcgcGxCanonicalLightingRecord) == 4,
    "canonical GX Lighting record ABI alignment changed"
);
ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalLightingState) ==
        ACGC_GX_CANONICAL_LIGHTING_STATE_SIZE,
    "canonical GX Lighting state ABI size changed"
);
ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    ACGC_GX_CANONICAL_LIGHTING_ALIGNOF(AcgcGxCanonicalLightingState) ==
        ACGC_GX_CANONICAL_LIGHTING_STATE_ALIGNMENT,
    "canonical GX Lighting state ABI alignment changed"
);
ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalLightingState, loaded_mask) == 0,
    "canonical GX Lighting loaded-mask offset changed"
);
ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalLightingState, records) == 4,
    "canonical GX Lighting records offset changed"
);
ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalLightingRecord, reserved) == 0,
    "canonical GX Lighting reserved offset changed"
);
ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalLightingRecord, color_rgba8) == 12,
    "canonical GX Lighting color offset changed"
);
ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalLightingRecord, angular_attenuation) == 16,
    "canonical GX Lighting angular-attenuation offset changed"
);
ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalLightingRecord, distance_attenuation) == 28,
    "canonical GX Lighting distance-attenuation offset changed"
);
ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalLightingRecord, position) == 40,
    "canonical GX Lighting position offset changed"
);
ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalLightingRecord, direction) == 52,
    "canonical GX Lighting direction offset changed"
);

#undef ACGC_GX_CANONICAL_LIGHTING_ALIGNOF
#undef ACGC_GX_CANONICAL_LIGHTING_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_LIGHTING_STATE_H */
