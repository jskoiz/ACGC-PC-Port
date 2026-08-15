#ifndef ACGC_GX_CANONICAL_ALPHA_STATE_H
#define ACGC_GX_CANONICAL_ALPHA_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the renderer-neutral, value-only Alpha test/update section for the
 * cumulative canonical GX envelope. The logical wire representation is eight
 * little-endian uint32 words in field order. The C ABI uses fixed-width
 * uint32_t members; a byte-stream owner must perform explicit little-endian
 * conversion rather than memcpy-ing this struct on a big-endian host.
 *
 * Both reference words remain present and bounded even when their comparison
 * makes one inactive. There is no comparison-dependent normalization and no
 * reserved tail.
 */
#define ACGC_GX_CANONICAL_ALPHA_STATE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_ALPHA_STATE_SIZE UINT32_C(32)
#define ACGC_GX_CANONICAL_ALPHA_STATE_ALIGNMENT UINT32_C(4)
#define ACGC_GX_CANONICAL_ALPHA_STATE_COUNT UINT32_C(1)
#define ACGC_GX_CANONICAL_ALPHA_STATE_CAPACITY UINT32_C(1)

#define ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX UINT32_C(7)
#define ACGC_GX_CANONICAL_ALPHA_REFERENCE_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_ALPHA_REFERENCE_MAX UINT32_C(255)
#define ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_ALPHA_OPERATOR_MAX UINT32_C(3)
#define ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX UINT32_C(1)

#define ACGC_GX_CANONICAL_ALPHA_SECTION_ID \
    ACGC_GX_CANONICAL_SECTION_ID_ALPHA
#define ACGC_GX_CANONICAL_ALPHA_SECTION_VERSION \
    ACGC_GX_CANONICAL_ALPHA_STATE_VERSION
#define ACGC_GX_CANONICAL_ALPHA_SECTION_MASK \
    ACGC_GX_CANONICAL_SECTION_MASK_ALPHA
#define ACGC_GX_CANONICAL_ALPHA_SECTION_BYTE_SIZE \
    ACGC_GX_CANONICAL_ALPHA_STATE_SIZE
#define ACGC_GX_CANONICAL_ALPHA_SECTION_COUNT \
    ACGC_GX_CANONICAL_ALPHA_STATE_COUNT
#define ACGC_GX_CANONICAL_ALPHA_SECTION_CAPACITY \
    ACGC_GX_CANONICAL_ALPHA_STATE_CAPACITY

typedef struct AcgcGxCanonicalAlphaState {
    uint32_t comp0;
    uint32_t ref0;
    uint32_t op;
    uint32_t comp1;
    uint32_t ref1;
    uint32_t color_update_enable;
    uint32_t alpha_update_enable;
    uint32_t z_comp_loc_before_tex;
} AcgcGxCanonicalAlphaState;

/* Return nonzero only for a non-null state with eight bounded GX words. */
int acgc_gx_canonical_alpha_state_validate(
    const AcgcGxCanonicalAlphaState* state
);

/*
 * Validate the common envelope, then enforce the exact Alpha entry metadata
 * when the section is present. The common validator intentionally keeps
 * non-fog sections generic; this narrow helper owns Alpha's exact size/count
 * and capacity rule without changing common envelope semantics. An absent
 * Alpha entry is valid only when its fixed section ID remains and all other
 * directory metadata words are zero.
 */
int acgc_gx_canonical_alpha_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_ALPHA_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_ALPHA_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT(
    sizeof(uint32_t) == 4,
    "canonical GX Alpha state requires 32-bit uint32_t"
);
ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT(
    ACGC_GX_CANONICAL_ALPHA_STATE_ALIGNMENT == 4,
    "canonical GX Alpha alignment contract changed"
);
ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalAlphaState) ==
        ACGC_GX_CANONICAL_ALPHA_STATE_SIZE,
    "canonical GX Alpha state ABI size changed"
);
ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT(
    ACGC_GX_CANONICAL_ALPHA_ALIGNOF(AcgcGxCanonicalAlphaState) ==
        ACGC_GX_CANONICAL_ALPHA_STATE_ALIGNMENT,
    "canonical GX Alpha state ABI alignment changed"
);
ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalAlphaState, comp0) == 0,
    "canonical GX Alpha comp0 offset changed"
);
ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalAlphaState, ref0) == 4,
    "canonical GX Alpha ref0 offset changed"
);
ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalAlphaState, op) == 8,
    "canonical GX Alpha op offset changed"
);
ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalAlphaState, comp1) == 12,
    "canonical GX Alpha comp1 offset changed"
);
ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalAlphaState, ref1) == 16,
    "canonical GX Alpha ref1 offset changed"
);
ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalAlphaState, color_update_enable) == 20,
    "canonical GX Alpha color-update offset changed"
);
ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalAlphaState, alpha_update_enable) == 24,
    "canonical GX Alpha alpha-update offset changed"
);
ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalAlphaState, z_comp_loc_before_tex) == 28,
    "canonical GX Alpha z-comp-location offset changed"
);

#undef ACGC_GX_CANONICAL_ALPHA_ALIGNOF
#undef ACGC_GX_CANONICAL_ALPHA_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_ALPHA_STATE_H */
