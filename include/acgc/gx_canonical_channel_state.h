#ifndef ACGC_GX_CANONICAL_CHANNEL_STATE_H
#define ACGC_GX_CANONICAL_CHANNEL_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the renderer-neutral, value-only Channels section for the
 * cumulative canonical GX envelope. The logical wire representation is
 * thirty-four little-endian uint32 words in field order. The C ABI uses
 * fixed-width uint32_t members; a byte-stream owner must perform explicit
 * little-endian conversion rather than memcpy-ing this struct on a
 * big-endian host.
 *
 * A color word is logical RGBA8: R occupies bits 0..7, G 8..15, B 16..23,
 * and A 24..31. There are no host floats, pointers, native enums, bools, or
 * bit-fields in this value ABI. Disabled controls retain their values and
 * remain subject to the same domain checks as enabled controls.
 */
#define ACGC_GX_CANONICAL_CHANNEL_STATE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_CHANNEL_STATE_SIZE UINT32_C(136)
#define ACGC_GX_CANONICAL_CHANNEL_STATE_ALIGNMENT UINT32_C(4)
#define ACGC_GX_CANONICAL_CHANNEL_STATE_COUNT UINT32_C(2)
#define ACGC_GX_CANONICAL_CHANNEL_STATE_CAPACITY UINT32_C(2)
#define ACGC_GX_CANONICAL_CHANNEL_RECORD_SIZE UINT32_C(64)
#define ACGC_GX_CANONICAL_CHANNEL_CONTROL_WORD_COUNT UINT32_C(6)

#define ACGC_GX_CANONICAL_CHANNEL_STATE_ACTIVE_COUNT_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_CHANNEL_STATE_ACTIVE_COUNT_MAX UINT32_C(2)
#define ACGC_GX_CANONICAL_CHANNEL_VALID_MASK_MAX UINT32_C(0x00000003)

#define ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_FALSE UINT32_C(0)
#define ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_TRUE UINT32_C(1)

#define ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG UINT32_C(0)
#define ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX UINT32_C(1)

#define ACGC_GX_CANONICAL_CHANNEL_LIGHT_MASK_MIN UINT32_C(0)
#define ACGC_GX_CANONICAL_CHANNEL_LIGHT_MASK_MAX UINT32_C(0x000000FF)

#define ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_NONE UINT32_C(0)
#define ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_SIGN UINT32_C(1)
#define ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_CLAMP UINT32_C(2)

#define ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_SPEC UINT32_C(0)
#define ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_SPOT UINT32_C(1)
#define ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_NONE UINT32_C(2)

#define ACGC_GX_CANONICAL_CHANNEL_SECTION_ID \
    ACGC_GX_CANONICAL_SECTION_ID_CHANNELS
#define ACGC_GX_CANONICAL_CHANNEL_SECTION_VERSION \
    ACGC_GX_CANONICAL_CHANNEL_STATE_VERSION
#define ACGC_GX_CANONICAL_CHANNEL_SECTION_MASK \
    ACGC_GX_CANONICAL_SECTION_MASK_CHANNELS
#define ACGC_GX_CANONICAL_CHANNEL_SECTION_BYTE_SIZE \
    ACGC_GX_CANONICAL_CHANNEL_STATE_SIZE
#define ACGC_GX_CANONICAL_CHANNEL_SECTION_COUNT \
    ACGC_GX_CANONICAL_CHANNEL_STATE_COUNT
#define ACGC_GX_CANONICAL_CHANNEL_SECTION_CAPACITY \
    ACGC_GX_CANONICAL_CHANNEL_STATE_CAPACITY

typedef struct AcgcGxCanonicalChannelControl {
    uint32_t enable;
    uint32_t ambient_source;
    uint32_t material_source;
    uint32_t light_mask;
    uint32_t diffuse_function;
    uint32_t attenuation_function;
} AcgcGxCanonicalChannelControl;

typedef struct AcgcGxCanonicalChannelRecord {
    uint32_t channel_index;
    uint32_t reserved;
    AcgcGxCanonicalChannelControl color;
    AcgcGxCanonicalChannelControl alpha;
    uint32_t ambient_rgba8;
    uint32_t material_rgba8;
} AcgcGxCanonicalChannelRecord;

typedef struct AcgcGxCanonicalChannelState {
    uint32_t active_count;
    uint32_t record_valid_mask;
    AcgcGxCanonicalChannelRecord records[
        ACGC_GX_CANONICAL_CHANNEL_STATE_CAPACITY];
} AcgcGxCanonicalChannelState;

/* Return nonzero only for a complete, structurally valid Channels value. */
int acgc_gx_canonical_channel_state_validate(
    const AcgcGxCanonicalChannelState* state
);

/*
 * Validate the common envelope, then enforce the exact Channels entry
 * metadata when the section is present. An absent entry is valid only when
 * its fixed section ID remains and all other directory metadata words are
 * zero. This helper validates metadata only; it does not invent a producer or
 * inspect payload bytes.
 */
int acgc_gx_canonical_channel_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

/* Encode exactly one fixed-size section; failures do not mutate output. */
int acgc_gx_canonical_channel_state_encode(
    const AcgcGxCanonicalChannelState* state,
    uint8_t* destination,
    size_t destination_byte_size
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_CHANNEL_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_CHANNEL_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    sizeof(uint32_t) == 4,
    "canonical GX Channels state requires 32-bit uint32_t"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    ACGC_GX_CANONICAL_CHANNEL_STATE_ALIGNMENT == 4,
    "canonical GX Channels alignment contract changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalChannelControl) == 24,
    "canonical GX Channels control ABI size changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    ACGC_GX_CANONICAL_CHANNEL_ALIGNOF(AcgcGxCanonicalChannelControl) == 4,
    "canonical GX Channels control ABI alignment changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalChannelRecord) ==
        ACGC_GX_CANONICAL_CHANNEL_RECORD_SIZE,
    "canonical GX Channels record ABI size changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    ACGC_GX_CANONICAL_CHANNEL_ALIGNOF(AcgcGxCanonicalChannelRecord) == 4,
    "canonical GX Channels record ABI alignment changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalChannelState) ==
        ACGC_GX_CANONICAL_CHANNEL_STATE_SIZE,
    "canonical GX Channels state ABI size changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    ACGC_GX_CANONICAL_CHANNEL_ALIGNOF(AcgcGxCanonicalChannelState) ==
        ACGC_GX_CANONICAL_CHANNEL_STATE_ALIGNMENT,
    "canonical GX Channels state ABI alignment changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelState, active_count) == 0,
    "canonical GX Channels active-count offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelState, record_valid_mask) == 4,
    "canonical GX Channels valid-mask offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelState, records) == 8,
    "canonical GX Channels records offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelRecord, channel_index) == 0,
    "canonical GX Channels channel-index offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelRecord, reserved) == 4,
    "canonical GX Channels record reserved offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelRecord, color) == 8,
    "canonical GX Channels color-control offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelRecord, alpha) == 32,
    "canonical GX Channels alpha-control offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelRecord, ambient_rgba8) == 56,
    "canonical GX Channels ambient-color offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelRecord, material_rgba8) == 60,
    "canonical GX Channels material-color offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelControl, enable) == 0,
    "canonical GX Channels enable offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelControl, ambient_source) == 4,
    "canonical GX Channels ambient-source offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelControl, material_source) == 8,
    "canonical GX Channels material-source offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelControl, light_mask) == 12,
    "canonical GX Channels light-mask offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelControl, diffuse_function) == 16,
    "canonical GX Channels diffuse-function offset changed"
);
ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalChannelControl, attenuation_function) == 20,
    "canonical GX Channels attenuation-function offset changed"
);

#undef ACGC_GX_CANONICAL_CHANNEL_ALIGNOF
#undef ACGC_GX_CANONICAL_CHANNEL_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_CHANNEL_STATE_H */
