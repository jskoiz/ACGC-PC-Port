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

/*
 * The cumulative packet is a fixed metadata prefix followed by a dynamic,
 * four-byte-aligned payload. The prefix deliberately describes sections but
 * does not embed any future section schema or a final packet-size constant.
 * All fields crossing this boundary are fixed-width words; the payload is not
 * a native pointer or a flexible host-owned object.
 */
#define ACGC_GX_CANONICAL_ENVELOPE_MAGIC UINT32_C(0x41434758)
#define ACGC_GX_CANONICAL_ENVELOPE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE UINT32_C(48)
#define ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE UINT32_C(32)
#define ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT UINT32_C(14)
#define ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT UINT32_C(4)
#define ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET \
    ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE
#define ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_SIZE \
    (ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT * \
     ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE)
#define ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET \
    (ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET + \
     ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_SIZE)
#define ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK UINT32_C(0x00003FFF)
#define ACGC_GX_CANONICAL_SECTION_VERSION UINT32_C(1)

/* Section IDs are stable directory-slot IDs, not GX or renderer enum values. */
#define ACGC_GX_CANONICAL_SECTION_ID_GEOMETRY UINT32_C(1)
#define ACGC_GX_CANONICAL_SECTION_ID_TRANSFORMS UINT32_C(2)
#define ACGC_GX_CANONICAL_SECTION_ID_CHANNELS UINT32_C(3)
#define ACGC_GX_CANONICAL_SECTION_ID_TEXGENS UINT32_C(4)
#define ACGC_GX_CANONICAL_SECTION_ID_TEXTURES UINT32_C(5)
#define ACGC_GX_CANONICAL_SECTION_ID_TEV UINT32_C(6)
#define ACGC_GX_CANONICAL_SECTION_ID_LIGHTING UINT32_C(7)
#define ACGC_GX_CANONICAL_SECTION_ID_BLEND UINT32_C(8)
#define ACGC_GX_CANONICAL_SECTION_ID_ALPHA UINT32_C(9)
#define ACGC_GX_CANONICAL_SECTION_ID_DEPTH UINT32_C(10)
#define ACGC_GX_CANONICAL_SECTION_ID_RASTER UINT32_C(11)
#define ACGC_GX_CANONICAL_SECTION_ID_FOG UINT32_C(12)
#define ACGC_GX_CANONICAL_SECTION_ID_INDIRECT UINT32_C(13)
#define ACGC_GX_CANONICAL_SECTION_ID_DYNAMIC UINT32_C(14)

#define ACGC_GX_CANONICAL_SECTION_MASK_GEOMETRY UINT32_C(0x0001)
#define ACGC_GX_CANONICAL_SECTION_MASK_TRANSFORMS UINT32_C(0x0002)
#define ACGC_GX_CANONICAL_SECTION_MASK_CHANNELS UINT32_C(0x0004)
#define ACGC_GX_CANONICAL_SECTION_MASK_TEXGENS UINT32_C(0x0008)
#define ACGC_GX_CANONICAL_SECTION_MASK_TEXTURES UINT32_C(0x0010)
#define ACGC_GX_CANONICAL_SECTION_MASK_TEV UINT32_C(0x0020)
#define ACGC_GX_CANONICAL_SECTION_MASK_LIGHTING UINT32_C(0x0040)
#define ACGC_GX_CANONICAL_SECTION_MASK_BLEND UINT32_C(0x0080)
#define ACGC_GX_CANONICAL_SECTION_MASK_ALPHA UINT32_C(0x0100)
#define ACGC_GX_CANONICAL_SECTION_MASK_DEPTH UINT32_C(0x0200)
#define ACGC_GX_CANONICAL_SECTION_MASK_RASTER UINT32_C(0x0400)
#define ACGC_GX_CANONICAL_SECTION_MASK_FOG UINT32_C(0x0800)
#define ACGC_GX_CANONICAL_SECTION_MASK_INDIRECT UINT32_C(0x1000)
#define ACGC_GX_CANONICAL_SECTION_MASK_DYNAMIC UINT32_C(0x2000)

typedef struct AcgcGxCanonicalEnvelopeHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t header_byte_size;
    uint32_t directory_entry_byte_size;
    uint32_t directory_count;
    uint32_t known_state_mask;
    uint32_t present_state_mask;
    uint32_t required_state_mask;
    uint32_t payload_offset;
    uint32_t payload_byte_size;
    uint32_t total_byte_size;
    uint32_t reserved;
} AcgcGxCanonicalEnvelopeHeader;

typedef struct AcgcGxCanonicalEnvelopeDirectoryEntry {
    uint32_t section_id;
    uint32_t section_version;
    uint32_t byte_offset;
    uint32_t byte_size;
    uint32_t count;
    uint32_t capacity;
    uint32_t valid_mask;
    uint32_t reserved;
} AcgcGxCanonicalEnvelopeDirectoryEntry;

/*
 * This is the complete fixed metadata prefix. Payload bytes begin at
 * ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET and are intentionally not modeled
 * here, so adding a section never silently freezes the cumulative total size.
 */
typedef struct AcgcGxCanonicalEnvelope {
    AcgcGxCanonicalEnvelopeHeader header;
    AcgcGxCanonicalEnvelopeDirectoryEntry directory[
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT];
} AcgcGxCanonicalEnvelope;

/* Return nonzero only for a complete, unmodified canonical fog section. */
int acgc_gx_canonical_fog_state_validate(
    const AcgcGxCanonicalFogState* state
);

/* Encode exactly one 80-byte Fog section; failures leave the destination unchanged. */
int acgc_gx_canonical_fog_state_encode(
    const AcgcGxCanonicalFogState* state,
    uint8_t* destination,
    size_t destination_byte_size
);

/* Initialize an empty, structurally valid metadata prefix. */
int acgc_gx_canonical_envelope_init(AcgcGxCanonicalEnvelope* envelope);

/*
 * Validate the fixed metadata prefix against the caller-owned total byte
 * extent. This checks directory structure and the exact fog entry metadata;
 * section payload contents remain owned by their individual section validators.
 */
int acgc_gx_canonical_envelope_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
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
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalEnvelopeHeader) ==
        ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE,
    "canonical GX envelope header ABI size changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalEnvelopeDirectoryEntry) ==
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE,
    "canonical GX envelope directory ABI size changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalEnvelope) ==
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET,
    "canonical GX envelope metadata size changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    ACGC_GX_CANONICAL_STATE_ALIGNOF(AcgcGxCanonicalEnvelopeHeader) ==
        ACGC_GX_CANONICAL_STATE_ALIGNOF(uint32_t),
    "canonical GX envelope header alignment changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    ACGC_GX_CANONICAL_STATE_ALIGNOF(AcgcGxCanonicalEnvelopeDirectoryEntry) ==
        ACGC_GX_CANONICAL_STATE_ALIGNOF(uint32_t),
    "canonical GX envelope directory alignment changed"
);
ACGC_GX_CANONICAL_STATE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalEnvelope, directory) ==
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET,
    "canonical GX envelope directory offset changed"
);

#undef ACGC_GX_CANONICAL_STATE_ALIGNOF
#undef ACGC_GX_CANONICAL_STATE_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_STATE_H */
