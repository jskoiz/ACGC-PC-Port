#ifndef ACGC_GX_CANONICAL_RASTER_STATE_H
#define ACGC_GX_CANONICAL_RASTER_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Renderer-neutral, value-only Raster state for the cumulative canonical GX
 * envelope. The wire representation is exactly 32 little-endian uint32
 * words. The six viewport words retain the exact finite IEEE-754 binary32
 * bit patterns supplied to GXSetViewport; they are not host float values.
 * Scissor offsets use signed fixed-width words, and every other field is a
 * logical GX value rather than a host enum, bool, bit-field, pointer, or GL
 * object. A byte-stream owner must explicitly convert words to little endian.
 *
 * Word order is frozen as follows:
 *   0..5   viewport bit patterns
 *   6..9   scissor left, top, width, height
 *   10..11 signed scissor x/y offsets
 *   12..14 clip mode, cull mode, co-planar enable
 *   15..18 line width/tex offset, point size/tex offset
 *   19..20 line/point texcoord enable masks
 *   21..23 dither, destination-alpha enable/value
 *   24..27 field mode, half aspect, odd/even field masks
 *   28..31 reserved zero words
 */
#define ACGC_GX_CANONICAL_RASTER_STATE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_RASTER_STATE_SIZE UINT32_C(128)
#define ACGC_GX_CANONICAL_RASTER_STATE_ALIGNMENT UINT32_C(4)
#define ACGC_GX_CANONICAL_RASTER_STATE_WORD_COUNT UINT32_C(32)
#define ACGC_GX_CANONICAL_RASTER_STATE_COUNT UINT32_C(1)
#define ACGC_GX_CANONICAL_RASTER_STATE_CAPACITY UINT32_C(1)

#define ACGC_GX_CANONICAL_RASTER_VIEWPORT_WORD_COUNT UINT32_C(6)
#define ACGC_GX_CANONICAL_RASTER_SCISSOR_WORD_COUNT UINT32_C(4)
#define ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_COUNT UINT32_C(2)
#define ACGC_GX_CANONICAL_RASTER_RESERVED_WORD_COUNT UINT32_C(4)

#define ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT UINT32_C(1706)
#define ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MIN (-INT32_C(342))
#define ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MAX INT32_C(1705)

#define ACGC_GX_CANONICAL_RASTER_CLIP_MODE_ENABLE UINT32_C(0)
#define ACGC_GX_CANONICAL_RASTER_CLIP_MODE_DISABLE UINT32_C(1)
#define ACGC_GX_CANONICAL_RASTER_CULL_MODE_NONE UINT32_C(0)
#define ACGC_GX_CANONICAL_RASTER_CULL_MODE_FRONT UINT32_C(1)
#define ACGC_GX_CANONICAL_RASTER_CULL_MODE_BACK UINT32_C(2)
#define ACGC_GX_CANONICAL_RASTER_CULL_MODE_ALL UINT32_C(3)

#define ACGC_GX_CANONICAL_RASTER_BOOLEAN_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX UINT32_C(1)
#define ACGC_GX_CANONICAL_RASTER_SIZE_MAX UINT32_C(255)
#define ACGC_GX_CANONICAL_RASTER_TEX_OFFSET_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_RASTER_TEX_OFFSET_MAX UINT32_C(5)
#define ACGC_GX_CANONICAL_RASTER_TEXCOORD_MASK_MAX UINT32_C(0x000000FF)
#define ACGC_GX_CANONICAL_RASTER_DST_ALPHA_MAX UINT32_C(255)

#define ACGC_GX_CANONICAL_RASTER_SECTION_ID \
    ACGC_GX_CANONICAL_SECTION_ID_RASTER
#define ACGC_GX_CANONICAL_RASTER_SECTION_VERSION \
    ACGC_GX_CANONICAL_RASTER_STATE_VERSION
#define ACGC_GX_CANONICAL_RASTER_SECTION_MASK \
    ACGC_GX_CANONICAL_SECTION_MASK_RASTER
#define ACGC_GX_CANONICAL_RASTER_SECTION_BYTE_SIZE \
    ACGC_GX_CANONICAL_RASTER_STATE_SIZE
#define ACGC_GX_CANONICAL_RASTER_SECTION_COUNT \
    ACGC_GX_CANONICAL_RASTER_STATE_COUNT
#define ACGC_GX_CANONICAL_RASTER_SECTION_CAPACITY \
    ACGC_GX_CANONICAL_RASTER_STATE_CAPACITY

#define ACGC_GX_CANONICAL_RASTER_VIEWPORT_OFFSET UINT32_C(0x00)
#define ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET UINT32_C(0x18)
#define ACGC_GX_CANONICAL_RASTER_SCISSOR_BOX_OFFSET_OFFSET UINT32_C(0x28)
#define ACGC_GX_CANONICAL_RASTER_CLIP_MODE_OFFSET UINT32_C(0x30)
#define ACGC_GX_CANONICAL_RASTER_CULL_MODE_OFFSET UINT32_C(0x34)
#define ACGC_GX_CANONICAL_RASTER_CO_PLANAR_OFFSET UINT32_C(0x38)
#define ACGC_GX_CANONICAL_RASTER_LINE_WIDTH_OFFSET UINT32_C(0x3C)
#define ACGC_GX_CANONICAL_RASTER_LINE_TEX_OFFSET_OFFSET UINT32_C(0x40)
#define ACGC_GX_CANONICAL_RASTER_POINT_SIZE_OFFSET UINT32_C(0x44)
#define ACGC_GX_CANONICAL_RASTER_POINT_TEX_OFFSET_OFFSET UINT32_C(0x48)
#define ACGC_GX_CANONICAL_RASTER_LINE_TEXCOORD_MASK_OFFSET UINT32_C(0x4C)
#define ACGC_GX_CANONICAL_RASTER_POINT_TEXCOORD_MASK_OFFSET UINT32_C(0x50)
#define ACGC_GX_CANONICAL_RASTER_DITHER_OFFSET UINT32_C(0x54)
#define ACGC_GX_CANONICAL_RASTER_DST_ALPHA_ENABLE_OFFSET UINT32_C(0x58)
#define ACGC_GX_CANONICAL_RASTER_DST_ALPHA_OFFSET UINT32_C(0x5C)
#define ACGC_GX_CANONICAL_RASTER_FIELD_MODE_OFFSET UINT32_C(0x60)
#define ACGC_GX_CANONICAL_RASTER_HALF_ASPECT_OFFSET UINT32_C(0x64)
#define ACGC_GX_CANONICAL_RASTER_FIELD_ODD_MASK_OFFSET UINT32_C(0x68)
#define ACGC_GX_CANONICAL_RASTER_FIELD_EVEN_MASK_OFFSET UINT32_C(0x6C)
#define ACGC_GX_CANONICAL_RASTER_RESERVED_OFFSET UINT32_C(0x70)
#define ACGC_GX_CANONICAL_RASTER_END_OFFSET UINT32_C(0x80)

typedef struct AcgcGxCanonicalRasterState {
    uint32_t viewport_bits[ACGC_GX_CANONICAL_RASTER_VIEWPORT_WORD_COUNT];
    uint32_t scissor[ACGC_GX_CANONICAL_RASTER_SCISSOR_WORD_COUNT];
    int32_t scissor_offset[ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_COUNT];
    uint32_t clip_mode;
    uint32_t cull_mode;
    uint32_t co_planar_enable;
    uint32_t line_width;
    uint32_t line_tex_offsets;
    uint32_t point_size;
    uint32_t point_tex_offsets;
    uint32_t line_texcoord_mask;
    uint32_t point_texcoord_mask;
    uint32_t dither;
    uint32_t dst_alpha_enable;
    uint32_t dst_alpha;
    uint32_t field_mode;
    uint32_t half_aspect_ratio;
    uint32_t field_odd_mask;
    uint32_t field_even_mask;
    uint32_t reserved[ACGC_GX_CANONICAL_RASTER_RESERVED_WORD_COUNT];
} AcgcGxCanonicalRasterState;

/* Return nonzero only for a complete, domain-valid Raster value state. */
int acgc_gx_canonical_raster_state_validate(
    const AcgcGxCanonicalRasterState* state
);

/* Validate only the exact Raster directory metadata in the common envelope. */
int acgc_gx_canonical_raster_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_RASTER_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_RASTER_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    sizeof(uint32_t) == 4,
    "canonical GX Raster state requires 32-bit uint32_t"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    sizeof(int32_t) == 4,
    "canonical GX Raster state requires 32-bit int32_t"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    ACGC_GX_CANONICAL_RASTER_STATE_ALIGNMENT == 4,
    "canonical GX Raster alignment contract changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalRasterState) ==
        ACGC_GX_CANONICAL_RASTER_STATE_SIZE,
    "canonical GX Raster state ABI size changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    ACGC_GX_CANONICAL_RASTER_ALIGNOF(AcgcGxCanonicalRasterState) ==
        ACGC_GX_CANONICAL_RASTER_STATE_ALIGNMENT,
    "canonical GX Raster state ABI alignment changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, viewport_bits) ==
        ACGC_GX_CANONICAL_RASTER_VIEWPORT_OFFSET,
    "canonical GX Raster viewport offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, scissor) ==
        ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET,
    "canonical GX Raster scissor offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, scissor_offset) ==
        ACGC_GX_CANONICAL_RASTER_SCISSOR_BOX_OFFSET_OFFSET,
    "canonical GX Raster scissor-box offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, clip_mode) ==
        ACGC_GX_CANONICAL_RASTER_CLIP_MODE_OFFSET,
    "canonical GX Raster clip-mode offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, cull_mode) ==
        ACGC_GX_CANONICAL_RASTER_CULL_MODE_OFFSET,
    "canonical GX Raster cull-mode offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, co_planar_enable) ==
        ACGC_GX_CANONICAL_RASTER_CO_PLANAR_OFFSET,
    "canonical GX Raster co-planar offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, line_width) ==
        ACGC_GX_CANONICAL_RASTER_LINE_WIDTH_OFFSET,
    "canonical GX Raster line-width offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, line_tex_offsets) ==
        ACGC_GX_CANONICAL_RASTER_LINE_TEX_OFFSET_OFFSET,
    "canonical GX Raster line-tex-offset offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, point_size) ==
        ACGC_GX_CANONICAL_RASTER_POINT_SIZE_OFFSET,
    "canonical GX Raster point-size offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, point_tex_offsets) ==
        ACGC_GX_CANONICAL_RASTER_POINT_TEX_OFFSET_OFFSET,
    "canonical GX Raster point-tex-offset offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, line_texcoord_mask) ==
        ACGC_GX_CANONICAL_RASTER_LINE_TEXCOORD_MASK_OFFSET,
    "canonical GX Raster line-texcoord-mask offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, point_texcoord_mask) ==
        ACGC_GX_CANONICAL_RASTER_POINT_TEXCOORD_MASK_OFFSET,
    "canonical GX Raster point-texcoord-mask offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, dither) ==
        ACGC_GX_CANONICAL_RASTER_DITHER_OFFSET,
    "canonical GX Raster dither offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, dst_alpha_enable) ==
        ACGC_GX_CANONICAL_RASTER_DST_ALPHA_ENABLE_OFFSET,
    "canonical GX Raster destination-alpha-enable offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, dst_alpha) ==
        ACGC_GX_CANONICAL_RASTER_DST_ALPHA_OFFSET,
    "canonical GX Raster destination-alpha offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, field_mode) ==
        ACGC_GX_CANONICAL_RASTER_FIELD_MODE_OFFSET,
    "canonical GX Raster field-mode offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, half_aspect_ratio) ==
        ACGC_GX_CANONICAL_RASTER_HALF_ASPECT_OFFSET,
    "canonical GX Raster half-aspect offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, field_odd_mask) ==
        ACGC_GX_CANONICAL_RASTER_FIELD_ODD_MASK_OFFSET,
    "canonical GX Raster odd-field-mask offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, field_even_mask) ==
        ACGC_GX_CANONICAL_RASTER_FIELD_EVEN_MASK_OFFSET,
    "canonical GX Raster even-field-mask offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalRasterState, reserved) ==
        ACGC_GX_CANONICAL_RASTER_RESERVED_OFFSET,
    "canonical GX Raster reserved offset changed"
);
ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT(
    ACGC_GX_CANONICAL_RASTER_END_OFFSET ==
        sizeof(AcgcGxCanonicalRasterState),
    "canonical GX Raster end offset changed"
);

#undef ACGC_GX_CANONICAL_RASTER_ALIGNOF
#undef ACGC_GX_CANONICAL_RASTER_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_RASTER_STATE_H */
