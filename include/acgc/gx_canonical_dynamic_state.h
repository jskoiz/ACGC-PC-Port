#ifndef ACGC_GX_CANONICAL_DYNAMIC_STATE_H
#define ACGC_GX_CANONICAL_DYNAMIC_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Renderer-neutral Dynamic/resource-sideband section.
 *
 * The logical wire representation is sixteen little-endian uint32 words in
 * the header followed by twenty-four sixteen-word records.  The record order
 * is image maps 0..7, then TLUT slots 0..15.  These C structs contain only
 * fixed-width value words; they do not own, point at, hash, or copy resource
 * bytes.  A byte-stream owner must explicitly convert each word to/from
 * little-endian instead of serializing host struct memory on a big-endian
 * host.
 */
#define ACGC_GX_CANONICAL_DYNAMIC_STATE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_DYNAMIC_STATE_SIZE UINT32_C(1600)
#define ACGC_GX_CANONICAL_DYNAMIC_STATE_ALIGNMENT UINT32_C(4)
#define ACGC_GX_CANONICAL_DYNAMIC_HEADER_WORD_COUNT UINT32_C(16)
#define ACGC_GX_CANONICAL_DYNAMIC_HEADER_BYTE_SIZE UINT32_C(64)
#define ACGC_GX_CANONICAL_DYNAMIC_RECORD_WORD_COUNT UINT32_C(16)
#define ACGC_GX_CANONICAL_DYNAMIC_RECORD_BYTE_SIZE UINT32_C(64)
#define ACGC_GX_CANONICAL_DYNAMIC_STATE_COUNT UINT32_C(24)
#define ACGC_GX_CANONICAL_DYNAMIC_STATE_CAPACITY UINT32_C(24)
#define ACGC_GX_CANONICAL_DYNAMIC_IMAGE_MAP_COUNT UINT32_C(8)
#define ACGC_GX_CANONICAL_DYNAMIC_TLUT_SLOT_COUNT UINT32_C(16)

#define ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID \
    ACGC_GX_CANONICAL_SECTION_ID_DYNAMIC
#define ACGC_GX_CANONICAL_DYNAMIC_SECTION_VERSION \
    ACGC_GX_CANONICAL_DYNAMIC_STATE_VERSION
#define ACGC_GX_CANONICAL_DYNAMIC_SECTION_MASK \
    ACGC_GX_CANONICAL_SECTION_MASK_DYNAMIC
#define ACGC_GX_CANONICAL_DYNAMIC_SECTION_BYTE_SIZE \
    ACGC_GX_CANONICAL_DYNAMIC_STATE_SIZE
#define ACGC_GX_CANONICAL_DYNAMIC_SECTION_COUNT \
    ACGC_GX_CANONICAL_DYNAMIC_STATE_COUNT
#define ACGC_GX_CANONICAL_DYNAMIC_SECTION_CAPACITY \
    ACGC_GX_CANONICAL_DYNAMIC_STATE_CAPACITY

#define ACGC_GX_CANONICAL_DYNAMIC_RESOURCE_ID_SCHEME UINT32_C(1)
#define ACGC_GX_CANONICAL_DYNAMIC_IMAGE_RESOURCE_ID_BASE UINT32_C(1)
#define ACGC_GX_CANONICAL_DYNAMIC_TLUT_RESOURCE_ID_BASE UINT32_C(0x100)

#define ACGC_GX_CANONICAL_DYNAMIC_KIND_IMAGE UINT32_C(1)
#define ACGC_GX_CANONICAL_DYNAMIC_KIND_TLUT UINT32_C(2)

#define ACGC_GX_CANONICAL_DYNAMIC_BYTES_UNAVAILABLE UINT32_C(0)
#define ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE UINT32_C(1)
#define ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED UINT32_C(2)
#define ACGC_GX_CANONICAL_DYNAMIC_BYTES_OWNED UINT32_C(4)
#define ACGC_GX_CANONICAL_DYNAMIC_BYTES_FLAG_MASK UINT32_C(7)

#define ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_GX_BE UINT32_C(0)
#define ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_LE UINT32_C(1)

#define ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES UINT32_C(32)
#define ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_RAW_GUEST UINT32_C(1)
#define ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_EMU64_CONVERTED UINT32_C(2)

#define ACGC_GX_CANONICAL_DYNAMIC_FORMAT_I4 UINT32_C(0)
#define ACGC_GX_CANONICAL_DYNAMIC_FORMAT_I8 UINT32_C(1)
#define ACGC_GX_CANONICAL_DYNAMIC_FORMAT_IA4 UINT32_C(2)
#define ACGC_GX_CANONICAL_DYNAMIC_FORMAT_IA8 UINT32_C(3)
#define ACGC_GX_CANONICAL_DYNAMIC_FORMAT_RGB565 UINT32_C(4)
#define ACGC_GX_CANONICAL_DYNAMIC_FORMAT_RGB5A3 UINT32_C(5)
#define ACGC_GX_CANONICAL_DYNAMIC_FORMAT_RGBA8 UINT32_C(6)
#define ACGC_GX_CANONICAL_DYNAMIC_FORMAT_C4 UINT32_C(8)
#define ACGC_GX_CANONICAL_DYNAMIC_FORMAT_C8 UINT32_C(9)
#define ACGC_GX_CANONICAL_DYNAMIC_FORMAT_C14X2 UINT32_C(10)
#define ACGC_GX_CANONICAL_DYNAMIC_FORMAT_CMPR UINT32_C(14)

#define ACGC_GX_CANONICAL_DYNAMIC_TLUT_FORMAT_IA8 UINT32_C(0)
#define ACGC_GX_CANONICAL_DYNAMIC_TLUT_FORMAT_RGB565 UINT32_C(1)
#define ACGC_GX_CANONICAL_DYNAMIC_TLUT_FORMAT_RGB5A3 UINT32_C(2)
#define ACGC_GX_CANONICAL_DYNAMIC_TLUT_ENTRY_MAX UINT32_C(0x4000)

typedef struct AcgcGxCanonicalDynamicHeader {
    /* Nonzero owner epoch for this value-sideband view. */
    uint32_t owner_epoch;
    uint32_t present_image_mask;
    uint32_t present_tlut_mask;
    uint32_t required_image_mask;
    uint32_t required_tlut_mask;
    /* Exact popcount(present_image_mask)+popcount(present_tlut_mask). */
    uint32_t present_resource_count;
    uint32_t record_byte_offset;
    uint32_t record_count;
    uint32_t record_capacity;
    uint32_t record_word_count;
    uint32_t resource_id_scheme;
    uint32_t reserved[5];
} AcgcGxCanonicalDynamicHeader;

typedef struct AcgcGxCanonicalDynamicRecord {
    uint32_t resource_id;
    uint32_t kind;
    uint32_t owner_epoch;
    uint32_t generation_lo;
    uint32_t generation_hi;
    /* Image map 0..7 or TLUT slot 0..15, not the directory index. */
    uint32_t owner_slot;
    uint32_t byte_flags;
    uint32_t byte_size;
    uint32_t byte_order;
    uint32_t alignment;
    uint32_t source_kind;
    uint32_t format;
    /* Zero for images; exact TLUT entry count for TLUT records. */
    uint32_t element_count;
    uint32_t reserved[3];
} AcgcGxCanonicalDynamicRecord;

typedef struct AcgcGxCanonicalDynamicState {
    AcgcGxCanonicalDynamicHeader header;
    AcgcGxCanonicalDynamicRecord records[
        ACGC_GX_CANONICAL_DYNAMIC_STATE_CAPACITY];
} AcgcGxCanonicalDynamicState;

/* Return nonzero only for a complete, structurally valid Dynamic value. */
int acgc_gx_canonical_dynamic_state_validate(
    const AcgcGxCanonicalDynamicState* state
);

/*
 * Encode validated fixed-width metadata into a caller-owned 0x640-byte
 * little-endian section. Resource payloads and borrow/lease state are not
 * inspected or copied; failures leave the destination unchanged.
 */
int acgc_gx_canonical_dynamic_state_encode(
    const AcgcGxCanonicalDynamicState* state,
    uint8_t* destination,
    size_t destination_byte_size
);

/* Validate only section-directory metadata in the common envelope. */
int acgc_gx_canonical_dynamic_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_DYNAMIC_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_DYNAMIC_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    sizeof(uint32_t) == 4,
    "canonical GX Dynamic state requires 32-bit uint32_t"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    ACGC_GX_CANONICAL_DYNAMIC_STATE_ALIGNMENT == 4,
    "canonical GX Dynamic alignment contract changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalDynamicHeader) ==
        ACGC_GX_CANONICAL_DYNAMIC_HEADER_BYTE_SIZE,
    "canonical GX Dynamic header ABI size changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    ACGC_GX_CANONICAL_DYNAMIC_ALIGNOF(AcgcGxCanonicalDynamicHeader) == 4,
    "canonical GX Dynamic header ABI alignment changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalDynamicRecord) ==
        ACGC_GX_CANONICAL_DYNAMIC_RECORD_BYTE_SIZE,
    "canonical GX Dynamic record ABI size changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    ACGC_GX_CANONICAL_DYNAMIC_ALIGNOF(AcgcGxCanonicalDynamicRecord) == 4,
    "canonical GX Dynamic record ABI alignment changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalDynamicState) ==
        ACGC_GX_CANONICAL_DYNAMIC_STATE_SIZE,
    "canonical GX Dynamic state ABI size changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    ACGC_GX_CANONICAL_DYNAMIC_ALIGNOF(AcgcGxCanonicalDynamicState) ==
        ACGC_GX_CANONICAL_DYNAMIC_STATE_ALIGNMENT,
    "canonical GX Dynamic state ABI alignment changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDynamicState, header) == 0,
    "canonical GX Dynamic header offset changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDynamicState, records) ==
        ACGC_GX_CANONICAL_DYNAMIC_HEADER_BYTE_SIZE,
    "canonical GX Dynamic records offset changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDynamicHeader, owner_epoch) == 0,
    "canonical GX Dynamic owner-epoch offset changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDynamicHeader, present_resource_count) == 20,
    "canonical GX Dynamic count offset changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDynamicHeader, record_byte_offset) == 24,
    "canonical GX Dynamic record-offset field changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDynamicHeader, reserved) == 44,
    "canonical GX Dynamic header reserved offset changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDynamicRecord, resource_id) == 0,
    "canonical GX Dynamic resource-ID offset changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDynamicRecord, byte_flags) == 24,
    "canonical GX Dynamic byte-flags offset changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDynamicRecord, element_count) == 48,
    "canonical GX Dynamic element-count offset changed"
);
ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalDynamicRecord, reserved) == 52,
    "canonical GX Dynamic record reserved offset changed"
);

#undef ACGC_GX_CANONICAL_DYNAMIC_ALIGNOF
#undef ACGC_GX_CANONICAL_DYNAMIC_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_DYNAMIC_STATE_H */
