#ifndef ACGC_GX_CANONICAL_TEXTURE_STATE_H
#define ACGC_GX_CANONICAL_TEXTURE_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "acgc/gx_canonical_dynamic_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Renderer-neutral Texture/TLUT value section.
 *
 * The logical wire representation is a sixteen-word little-endian header
 * followed by eight fixed thirty-six-word map records.  The C structs use
 * only uint32_t words and are not a serialization of GXTexObj, GXTlutObj,
 * PCGXTextureSource, or any host object.  Resource bytes and their leases
 * remain in the separate Dynamic section and in the owning runtime.  A
 * byte-stream owner must explicitly convert every word to little-endian.
 *
 * Map n owns image identity 1+n.  An indexed map's TLUT name owns identity
 * 0x100+n.  Zero is the absent image/TLUT identity; a known non-indexed map
 * uses UINT32_MAX as its explicit no-TLUT name sentinel.
 */
#define ACGC_GX_CANONICAL_TEXTURE_STATE_VERSION UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE UINT32_C(1216)
#define ACGC_GX_CANONICAL_TEXTURE_STATE_ALIGNMENT UINT32_C(4)
#define ACGC_GX_CANONICAL_TEXTURE_HEADER_WORD_COUNT UINT32_C(16)
#define ACGC_GX_CANONICAL_TEXTURE_HEADER_BYTE_SIZE UINT32_C(64)
#define ACGC_GX_CANONICAL_TEXTURE_RECORD_WORD_COUNT UINT32_C(36)
#define ACGC_GX_CANONICAL_TEXTURE_RECORD_BYTE_SIZE UINT32_C(144)
#define ACGC_GX_CANONICAL_TEXTURE_STATE_COUNT UINT32_C(8)
#define ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY UINT32_C(8)
#define ACGC_GX_CANONICAL_TEXTURE_TLUT_NAME_NONE UINT32_MAX

#define ACGC_GX_CANONICAL_TEXTURE_SECTION_ID \
    ACGC_GX_CANONICAL_SECTION_ID_TEXTURES
#define ACGC_GX_CANONICAL_TEXTURE_SECTION_VERSION \
    ACGC_GX_CANONICAL_TEXTURE_STATE_VERSION
#define ACGC_GX_CANONICAL_TEXTURE_SECTION_MASK \
    ACGC_GX_CANONICAL_SECTION_MASK_TEXTURES
#define ACGC_GX_CANONICAL_TEXTURE_SECTION_BYTE_SIZE \
    ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE
#define ACGC_GX_CANONICAL_TEXTURE_SECTION_COUNT \
    ACGC_GX_CANONICAL_TEXTURE_STATE_COUNT
#define ACGC_GX_CANONICAL_TEXTURE_SECTION_CAPACITY \
    ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY

#define ACGC_GX_CANONICAL_TEXTURE_RESOURCE_ID_SCHEME UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXTURE_IMAGE_RESOURCE_ID_BASE UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXTURE_TLUT_RESOURCE_ID_BASE UINT32_C(0x100)

#define ACGC_GX_CANONICAL_TEXTURE_FLAG_INDEXED UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXTURE_FLAG_MIPMAP UINT32_C(2)
#define ACGC_GX_CANONICAL_TEXTURE_FLAG_TLUT_PRESENT UINT32_C(4)
#define ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED UINT32_C(8)
#define ACGC_GX_CANONICAL_TEXTURE_FLAG_MASK UINT32_C(0xF)

#define ACGC_GX_CANONICAL_TEXTURE_WRAP_CLAMP UINT32_C(0)
#define ACGC_GX_CANONICAL_TEXTURE_WRAP_REPEAT UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXTURE_WRAP_MIRROR UINT32_C(2)
#define ACGC_GX_CANONICAL_TEXTURE_FILTER_MAX UINT32_C(5)
#define ACGC_GX_CANONICAL_TEXTURE_ANISOTROPY_MAX UINT32_C(2)
#define ACGC_GX_CANONICAL_TEXTURE_DIMENSION_MIN UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXTURE_DIMENSION_MAX UINT32_C(1024)
#define ACGC_GX_CANONICAL_TEXTURE_LOD_Q4_MAX UINT32_C(160)
#define ACGC_GX_CANONICAL_TEXTURE_LOD_BIAS_Q5_MIN (-128)
#define ACGC_GX_CANONICAL_TEXTURE_LOD_BIAS_Q5_MAX (127)
#define ACGC_GX_CANONICAL_TEXTURE_MIP_LEVEL_COUNT_MAX UINT32_C(11)
#define ACGC_GX_CANONICAL_TEXTURE_TLUT_NAME_MAX UINT32_C(15)

#define ACGC_GX_CANONICAL_TEXTURE_FORMAT_I4 UINT32_C(0)
#define ACGC_GX_CANONICAL_TEXTURE_FORMAT_I8 UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXTURE_FORMAT_IA4 UINT32_C(2)
#define ACGC_GX_CANONICAL_TEXTURE_FORMAT_IA8 UINT32_C(3)
#define ACGC_GX_CANONICAL_TEXTURE_FORMAT_RGB565 UINT32_C(4)
#define ACGC_GX_CANONICAL_TEXTURE_FORMAT_RGB5A3 UINT32_C(5)
#define ACGC_GX_CANONICAL_TEXTURE_FORMAT_RGBA8 UINT32_C(6)
#define ACGC_GX_CANONICAL_TEXTURE_FORMAT_C4 UINT32_C(8)
#define ACGC_GX_CANONICAL_TEXTURE_FORMAT_C8 UINT32_C(9)
#define ACGC_GX_CANONICAL_TEXTURE_FORMAT_C14X2 UINT32_C(10)
#define ACGC_GX_CANONICAL_TEXTURE_FORMAT_CMPR UINT32_C(14)

#define ACGC_GX_CANONICAL_TEXTURE_TLUT_FORMAT_IA8 UINT32_C(0)
#define ACGC_GX_CANONICAL_TEXTURE_TLUT_FORMAT_RGB565 UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXTURE_TLUT_FORMAT_RGB5A3 UINT32_C(2)
#define ACGC_GX_CANONICAL_TEXTURE_TLUT_ENTRY_MAX UINT32_C(0x4000)

#define ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_GX_BE UINT32_C(0)
#define ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_LE UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST UINT32_C(1)
#define ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_EMU64_CONVERTED UINT32_C(2)

typedef struct AcgcGxCanonicalTextureHeader {
    uint32_t known_map_mask;
    /* Exact popcount(known_map_mask). */
    uint32_t known_map_count;
    uint32_t indexed_map_mask;
    uint32_t mipmap_map_mask;
    uint32_t tlut_present_map_mask;
    uint32_t required_map_mask;
    uint32_t record_byte_offset;
    uint32_t record_count;
    uint32_t record_capacity;
    uint32_t record_word_count;
    uint32_t resource_id_scheme;
    uint32_t reserved[5];
} AcgcGxCanonicalTextureHeader;

typedef struct AcgcGxCanonicalTextureRecord {
    uint32_t flags;
    uint32_t image_resource_id;
    uint32_t image_owner_epoch;
    uint32_t image_generation_lo;
    uint32_t image_generation_hi;
    uint32_t width;
    uint32_t height;
    uint32_t image_format;
    uint32_t wrap_s;
    uint32_t wrap_t;
    uint32_t min_filter;
    uint32_t mag_filter;
    uint32_t min_lod_q4;
    uint32_t max_lod_q4;
    /* Signed Q5 value represented in a uint32_t word. */
    uint32_t lod_bias_q5;
    uint32_t bias_clamp;
    uint32_t edge_lod;
    uint32_t max_anisotropy;
    uint32_t mip_level_count;
    uint32_t image_byte_size;
    uint32_t image_byte_order;
    uint32_t image_source_kind;
    uint32_t tlut_resource_id;
    uint32_t tlut_owner_epoch;
    uint32_t tlut_generation_lo;
    uint32_t tlut_generation_hi;
    uint32_t tlut_name;
    uint32_t tlut_format;
    uint32_t tlut_entry_count;
    uint32_t tlut_byte_size;
    uint32_t tlut_byte_order;
    uint32_t tlut_source_kind;
    uint32_t reserved[4];
} AcgcGxCanonicalTextureRecord;

typedef struct AcgcGxCanonicalTextureState {
    AcgcGxCanonicalTextureHeader header;
    AcgcGxCanonicalTextureRecord records[
        ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY];
} AcgcGxCanonicalTextureState;

/* Return nonzero only for a complete, structurally valid Texture value. */
int acgc_gx_canonical_texture_state_validate(
    const AcgcGxCanonicalTextureState* state
);

/* Validate only section-directory metadata in the common envelope. */
int acgc_gx_canonical_texture_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

/*
 * Cross-validate value metadata only. Required Texture resources must have
 * matching Dynamic identity/epoch/generation/byte metadata and an available
 * Dynamic lease flag. No function in this ABI reads resource bytes or stores
 * a lease pointer.
 */
int acgc_gx_canonical_texture_dynamic_validate(
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic
);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT static_assert
#define ACGC_GX_CANONICAL_TEXTURE_ALIGNOF(type) alignof(type)
#else
#define ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT _Static_assert
#define ACGC_GX_CANONICAL_TEXTURE_ALIGNOF(type) _Alignof(type)
#endif

ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    sizeof(uint32_t) == 4,
    "canonical GX Texture state requires 32-bit uint32_t"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    ACGC_GX_CANONICAL_TEXTURE_STATE_ALIGNMENT == 4,
    "canonical GX Texture alignment contract changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalTextureHeader) ==
        ACGC_GX_CANONICAL_TEXTURE_HEADER_BYTE_SIZE,
    "canonical GX Texture header ABI size changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    ACGC_GX_CANONICAL_TEXTURE_ALIGNOF(AcgcGxCanonicalTextureHeader) == 4,
    "canonical GX Texture header ABI alignment changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalTextureRecord) ==
        ACGC_GX_CANONICAL_TEXTURE_RECORD_BYTE_SIZE,
    "canonical GX Texture record ABI size changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    ACGC_GX_CANONICAL_TEXTURE_ALIGNOF(AcgcGxCanonicalTextureRecord) == 4,
    "canonical GX Texture record ABI alignment changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    sizeof(AcgcGxCanonicalTextureState) ==
        ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE,
    "canonical GX Texture state ABI size changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    ACGC_GX_CANONICAL_TEXTURE_ALIGNOF(AcgcGxCanonicalTextureState) ==
        ACGC_GX_CANONICAL_TEXTURE_STATE_ALIGNMENT,
    "canonical GX Texture state ABI alignment changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTextureState, header) == 0,
    "canonical GX Texture header offset changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTextureState, records) ==
        ACGC_GX_CANONICAL_TEXTURE_HEADER_BYTE_SIZE,
    "canonical GX Texture records offset changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTextureHeader, known_map_mask) == 0,
    "canonical GX Texture known-mask offset changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTextureHeader, known_map_count) == 4,
    "canonical GX Texture count offset changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTextureHeader, record_byte_offset) == 24,
    "canonical GX Texture record-offset field changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTextureHeader, reserved) == 44,
    "canonical GX Texture header reserved offset changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTextureRecord, flags) == 0,
    "canonical GX Texture flags offset changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTextureRecord, image_resource_id) == 4,
    "canonical GX Texture image-ID offset changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTextureRecord, tlut_resource_id) == 88,
    "canonical GX Texture TLUT-ID offset changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTextureRecord, tlut_name) == 104,
    "canonical GX Texture TLUT-name offset changed"
);
ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT(
    offsetof(AcgcGxCanonicalTextureRecord, reserved) == 128,
    "canonical GX Texture record reserved offset changed"
);

#undef ACGC_GX_CANONICAL_TEXTURE_ALIGNOF
#undef ACGC_GX_CANONICAL_TEXTURE_STATIC_ASSERT

#endif /* ACGC_GX_CANONICAL_TEXTURE_STATE_H */
