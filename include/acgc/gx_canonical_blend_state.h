#ifndef ACGC_GX_CANONICAL_BLEND_STATE_H
#define ACGC_GX_CANONICAL_BLEND_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the renderer-neutral, value-only Blend/logic section for the
 * cumulative canonical GX envelope. The logical wire representation is four
 * little-endian uint32 words in field order. The C ABI uses fixed-width
 * uint32_t members; a byte-stream owner must perform explicit little-endian
 * conversion rather than memcpy-ing this struct on a big-endian host.
 *
 * Blend mode, source factor, destination factor, and logic operation remain
 * present and independently bounded even when a GX mode does not consume one
 * of the fields. There is no reserved tail and no mode-dependent
 * normalization.
 */
#define ACGC_GX_CANONICAL_BLEND_STATE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_BLEND_STATE_SIZE UINT32_C(16)
#define ACGC_GX_CANONICAL_BLEND_STATE_ALIGNMENT UINT32_C(4)
#define ACGC_GX_CANONICAL_BLEND_STATE_COUNT UINT32_C(1)
#define ACGC_GX_CANONICAL_BLEND_STATE_CAPACITY UINT32_C(1)

#define ACGC_GX_CANONICAL_BLEND_MODE_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_BLEND_MODE_MAX UINT32_C(3)
#define ACGC_GX_CANONICAL_BLEND_FACTOR_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_BLEND_FACTOR_MAX UINT32_C(7)
#define ACGC_GX_CANONICAL_BLEND_LOGIC_OP_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_BLEND_LOGIC_OP_MAX UINT32_C(15)

#define ACGC_GX_CANONICAL_BLEND_SECTION_ID \
    ACGC_GX_CANONICAL_SECTION_ID_BLEND
#define ACGC_GX_CANONICAL_BLEND_SECTION_VERSION \
    ACGC_GX_CANONICAL_BLEND_STATE_VERSION
#define ACGC_GX_CANONICAL_BLEND_SECTION_MASK \
    ACGC_GX_CANONICAL_SECTION_MASK_BLEND
#define ACGC_GX_CANONICAL_BLEND_SECTION_BYTE_SIZE \
    ACGC_GX_CANONICAL_BLEND_STATE_SIZE
#define ACGC_GX_CANONICAL_BLEND_SECTION_COUNT \
    ACGC_GX_CANONICAL_BLEND_STATE_COUNT
#define ACGC_GX_CANONICAL_BLEND_SECTION_CAPACITY \
    ACGC_GX_CANONICAL_BLEND_STATE_CAPACITY

typedef struct AcgcGxCanonicalBlendState {
    uint32_t mode;
    uint32_t source_factor;
    uint32_t destination_factor;
    uint32_t logic_op;
} AcgcGxCanonicalBlendState;

/* Return nonzero only for a non-null state with four bounded GX words. */
int acgc_gx_canonical_blend_state_validate(
    const AcgcGxCanonicalBlendState* state
);

/* Encode exactly one 16-byte Blend section; failures leave the destination unchanged. */
int acgc_gx_canonical_blend_state_encode(
    const AcgcGxCanonicalBlendState* state,
    uint8_t* destination,
    size_t destination_byte_size
);

/*
 * Validate the common envelope, then enforce the exact Blend entry metadata
 * when the section is present. The common validator intentionally keeps
 * non-fog sections generic; this narrow helper owns Blend's exact size/count
 * and capacity rule without changing common envelope semantics. An absent
 * Blend entry is valid only when its fixed section ID remains and all other
 * directory metadata words are zero.
 */
int acgc_gx_canonical_blend_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_BLEND_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_BLEND_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_BLEND_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_BLEND_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_BLEND_STATIC_ASSERT(
    sizeof(uint32_t) == 4,
    "canonical GX Blend state requires 32-bit uint32_t"
);
ACGC_GX_CANONICAL_BLEND_STATIC_ASSERT(
    ACGC_GX_CANONICAL_BLEND_STATE_ALIGNMENT == 4,
    "canonical GX Blend alignment contract changed"
);
ACGC_GX_CANONICAL_BLEND_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalBlendState) ==
        ACGC_GX_CANONICAL_BLEND_STATE_SIZE,
    "canonical GX Blend state ABI size changed"
);
ACGC_GX_CANONICAL_BLEND_STATIC_ASSERT(
    ACGC_GX_CANONICAL_BLEND_ALIGNOF(AcgcGxCanonicalBlendState) ==
        ACGC_GX_CANONICAL_BLEND_STATE_ALIGNMENT,
    "canonical GX Blend state ABI alignment changed"
);
ACGC_GX_CANONICAL_BLEND_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalBlendState, mode) == 0,
    "canonical GX Blend mode offset changed"
);
ACGC_GX_CANONICAL_BLEND_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalBlendState, source_factor) == 4,
    "canonical GX Blend source-factor offset changed"
);
ACGC_GX_CANONICAL_BLEND_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalBlendState, destination_factor) == 8,
    "canonical GX Blend destination-factor offset changed"
);
ACGC_GX_CANONICAL_BLEND_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalBlendState, logic_op) == 12,
    "canonical GX Blend logic-op offset changed"
);

#undef ACGC_GX_CANONICAL_BLEND_ALIGNOF
#undef ACGC_GX_CANONICAL_BLEND_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_BLEND_STATE_H */
