#ifndef ACGC_GX_CANONICAL_STATE_H
#define ACGC_GX_CANONICAL_STATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is a value-only fog section for a later cumulative canonical packet.
 * It is deliberately independent of the existing semantic packet versions
 * and is not a V5 packet or a renderer handoff.
 *
 * The logical boundary is twenty little-endian uint32 words.  The C ABI uses
 * uint32_t values for those words; a byte-stream owner must perform explicit
 * little-endian word conversion instead of memcpy-ing this struct on a
 * big-endian host.  No field is a host float, pointer, enum, or bit-field.
 *
 * color_rgba8 is packed by logical bits, with R in bits 0..7, G in 8..15,
 * B in 16..23, and A in 24..31.  This keeps the logical RGBA byte order
 * explicit even when the host's memory byte order differs.
 */
#define ACGC_GX_CANONICAL_FOG_STATE_SIZE UINT32_C(80)
#define ACGC_GX_CANONICAL_FOG_RANGE_COUNT UINT32_C(10)
#define ACGC_GX_CANONICAL_FOG_RESERVED_WORD_COUNT UINT32_C(2)
#define ACGC_GX_CANONICAL_FOG_RANGE_VALUE_MASK UINT32_C(0x00000FFF)
#define ACGC_GX_CANONICAL_FOG_CENTER_SOURCE_MAX UINT32_C(0x0000FFFF)

/* GXSetFogRangeAdj packs center + 342 into a 10-bit register field. */
#define ACGC_GX_CANONICAL_FOG_CENTER_MAX UINT32_C(681)

#define ACGC_GX_CANONICAL_FOG_TYPE_NONE UINT32_C(0)
#define ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN UINT32_C(2)
#define ACGC_GX_CANONICAL_FOG_TYPE_PERSP_EXP UINT32_C(4)
#define ACGC_GX_CANONICAL_FOG_TYPE_PERSP_EXP2 UINT32_C(5)
#define ACGC_GX_CANONICAL_FOG_TYPE_PERSP_REVEXP UINT32_C(6)
#define ACGC_GX_CANONICAL_FOG_TYPE_PERSP_REVEXP2 UINT32_C(7)
#define ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_LIN UINT32_C(10)
#define ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_EXP UINT32_C(12)
#define ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_EXP2 UINT32_C(13)
#define ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_REVEXP UINT32_C(14)
#define ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_REVEXP2 UINT32_C(15)

typedef struct AcgcGxCanonicalFogState {
    /* Canonical GX fog type value, not a host enum object. */
    uint32_t fog_type;
    /* IEEE-754 binary32 bit patterns in GXSetFog argument order. */
    uint32_t start_bits;
    uint32_t end_bits;
    uint32_t near_bits;
    uint32_t far_bits;
    /* Logical RGBA8 packing described above. */
    uint32_t color_rgba8;
    /* Canonical GXBool: exactly zero or one. */
    uint32_t range_adjust_enable;
    /* Widened GX u16 center; the active register limit is documented above. */
    uint32_t range_center;
    /* Widened GX u16 entries; each entry is a 12-bit value. */
    uint32_t range_adjust[ACGC_GX_CANONICAL_FOG_RANGE_COUNT];
    /* Must remain all zero at this ABI boundary. */
    uint32_t reserved[ACGC_GX_CANONICAL_FOG_RESERVED_WORD_COUNT];
} AcgcGxCanonicalFogState;

/* Return nonzero only for a complete, unmodified canonical fog section. */
int acgc_gx_canonical_fog_state_validate(
    const AcgcGxCanonicalFogState* state
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_STATE_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_STATE_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_STATE_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_STATE_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    sizeof(uint32_t) == 4,
    "canonical GX state requires 32-bit uint32_t"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalFogState) == ACGC_GX_CANONICAL_FOG_STATE_SIZE,
    "canonical GX fog state ABI size changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    ACGC_GX_CANONICAL_STATE_ALIGNOF(AcgcGxCanonicalFogState) ==
        ACGC_GX_CANONICAL_STATE_ALIGNOF(uint32_t),
    "canonical GX fog state ABI alignment changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalFogState, fog_type) == 0,
    "canonical GX fog type offset changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalFogState, start_bits) == 4,
    "canonical GX fog start offset changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalFogState, end_bits) == 8,
    "canonical GX fog end offset changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalFogState, near_bits) == 12,
    "canonical GX fog near offset changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalFogState, far_bits) == 16,
    "canonical GX fog far offset changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalFogState, color_rgba8) == 20,
    "canonical GX fog color offset changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalFogState, range_adjust_enable) == 24,
    "canonical GX range-enable offset changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalFogState, range_center) == 28,
    "canonical GX range-center offset changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalFogState, range_adjust) == 32,
    "canonical GX range-table offset changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalFogState, reserved) == 72,
    "canonical GX reserved offset changed"
);

#undef ACGC_GX_CANONICAL_STATE_ALIGNOF
#undef ACGC_GX_CANONICAL_STATE_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_STATE_H */
