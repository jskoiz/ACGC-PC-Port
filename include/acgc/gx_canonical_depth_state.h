#ifndef ACGC_GX_CANONICAL_DEPTH_STATE_H
#define ACGC_GX_CANONICAL_DEPTH_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the renderer-neutral, value-only Depth section for the cumulative
 * canonical GX envelope. The logical wire representation is four little-
 * endian uint32 words in field order. The C ABI uses fixed-width uint32_t
 * members; a byte-stream owner must perform explicit little-endian conversion
 * rather than memcpy-ing this struct on a big-endian host.
 *
 * The compare function remains present and bounded even when comparison is
 * disabled. The final word is reserved and must remain zero. No field is a
 * pointer, host enum, host bool, or bit-field.
 */
#define ACGC_GX_CANONICAL_DEPTH_STATE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_DEPTH_STATE_SIZE UINT32_C(16)
#define ACGC_GX_CANONICAL_DEPTH_STATE_ALIGNMENT UINT32_C(4)
#define ACGC_GX_CANONICAL_DEPTH_STATE_COUNT UINT32_C(1)
#define ACGC_GX_CANONICAL_DEPTH_STATE_CAPACITY UINT32_C(1)

#define ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_DEPTH_BOOLEAN_MAX UINT32_C(1)
#define ACGC_GX_CANONICAL_DEPTH_COMPARE_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_DEPTH_COMPARE_MAX UINT32_C(7)

#define ACGC_GX_CANONICAL_DEPTH_SECTION_ID \
    ACGC_GX_CANONICAL_SECTION_ID_DEPTH
#define ACGC_GX_CANONICAL_DEPTH_SECTION_VERSION \
    ACGC_GX_CANONICAL_DEPTH_STATE_VERSION
#define ACGC_GX_CANONICAL_DEPTH_SECTION_MASK \
    ACGC_GX_CANONICAL_SECTION_MASK_DEPTH
#define ACGC_GX_CANONICAL_DEPTH_SECTION_BYTE_SIZE \
    ACGC_GX_CANONICAL_DEPTH_STATE_SIZE
#define ACGC_GX_CANONICAL_DEPTH_SECTION_COUNT \
    ACGC_GX_CANONICAL_DEPTH_STATE_COUNT
#define ACGC_GX_CANONICAL_DEPTH_SECTION_CAPACITY \
    ACGC_GX_CANONICAL_DEPTH_STATE_CAPACITY

typedef struct AcgcGxCanonicalDepthState {
    uint32_t z_compare_enable;
    uint32_t z_compare_func;
    uint32_t z_update_enable;
    uint32_t reserved;
} AcgcGxCanonicalDepthState;

/* Return nonzero only for a non-null state with four valid GX words. */
int acgc_gx_canonical_depth_state_validate(
    const AcgcGxCanonicalDepthState* state
);

/*
 * Validate the common envelope, then enforce the exact Depth entry metadata
 * when the section is present. An absent Depth entry is valid only when its
 * fixed section ID remains and all other directory metadata words are zero.
 */
int acgc_gx_canonical_depth_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_DEPTH_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_DEPTH_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_DEPTH_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_DEPTH_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_DEPTH_STATIC_ASSERT(
    sizeof(uint32_t) == 4,
    "canonical GX Depth state requires 32-bit uint32_t"
);
ACGC_GX_CANONICAL_DEPTH_STATIC_ASSERT(
    ACGC_GX_CANONICAL_DEPTH_STATE_ALIGNMENT == 4,
    "canonical GX Depth alignment contract changed"
);
ACGC_GX_CANONICAL_DEPTH_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalDepthState) ==
        ACGC_GX_CANONICAL_DEPTH_STATE_SIZE,
    "canonical GX Depth state ABI size changed"
);
ACGC_GX_CANONICAL_DEPTH_STATIC_ASSERT(
    ACGC_GX_CANONICAL_DEPTH_ALIGNOF(AcgcGxCanonicalDepthState) ==
        ACGC_GX_CANONICAL_DEPTH_STATE_ALIGNMENT,
    "canonical GX Depth state ABI alignment changed"
);
ACGC_GX_CANONICAL_DEPTH_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDepthState, z_compare_enable) == 0,
    "canonical GX Depth compare-enable offset changed"
);
ACGC_GX_CANONICAL_DEPTH_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDepthState, z_compare_func) == 4,
    "canonical GX Depth compare-function offset changed"
);
ACGC_GX_CANONICAL_DEPTH_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDepthState, z_update_enable) == 8,
    "canonical GX Depth update-enable offset changed"
);
ACGC_GX_CANONICAL_DEPTH_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDepthState, reserved) == 12,
    "canonical GX Depth reserved offset changed"
);

#undef ACGC_GX_CANONICAL_DEPTH_ALIGNOF
#undef ACGC_GX_CANONICAL_DEPTH_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_DEPTH_STATE_H */
