#ifndef PC_GX_TEXTURE_RAW_STATE_H
#define PC_GX_TEXTURE_RAW_STATE_H

#include <stdint.h>

#include "acgc/gx_canonical_dynamic_state.h"
#include "acgc/gx_canonical_texture_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_GX_TEXTURE_RAW_MAP_COUNT UINT32_C(8)
#define PC_GX_TEXTURE_RAW_TLUT_COUNT UINT32_C(16)

#define PC_GX_TEXTURE_RAW_SOURCE_RAW_GUEST \
    ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST
#define PC_GX_TEXTURE_RAW_SOURCE_EMU64_CONVERTED \
    ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_EMU64_CONVERTED

#define PC_GX_TEXTURE_RAW_BYTE_ORDER_GX_BE \
    ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_GX_BE
#define PC_GX_TEXTURE_RAW_BYTE_ORDER_LE \
    ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_LE

/*
 * Setter-owned Texture/TLUT value state.  These records deliberately contain
 * no host pointers, GL names, cache hashes, native GX enum types, size_t, or
 * host boolean types.  Borrowed bytes live in the separate lease sideband
 * below and are valid only for one synchronous callback.
 */
typedef struct PCGXTextureRawMapRecord {
    uint32_t logical_id;
    uint32_t image_resource_id;
    uint32_t tlut_resource_id;
    uint32_t tlut_name;
    uint32_t known;
    uint32_t indexed;
    uint32_t mipmap;
    uint32_t invalid;
} PCGXTextureRawMapRecord;

typedef struct PCGXTextureRawImageResource {
    uint32_t logical_id;
    uint32_t owner_epoch;
    uint64_t generation;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t wrap_s;
    uint32_t wrap_t;
    uint32_t min_filter;
    uint32_t mag_filter;
    uint32_t min_lod_q4;
    uint32_t max_lod_q4;
    uint32_t lod_bias_q5;
    uint32_t bias_clamp;
    uint32_t edge_lod;
    uint32_t max_anisotropy;
    uint32_t mip_level_count;
    uint32_t byte_size;
    uint32_t byte_order;
    uint32_t source_kind;
    uint32_t available;
    uint32_t reserved[3];
} PCGXTextureRawImageResource;

typedef struct PCGXTextureRawTlutResource {
    uint32_t logical_id;
    uint32_t owner_epoch;
    uint64_t generation;
    uint32_t owner_slot;
    uint32_t format;
    uint32_t entry_count;
    uint32_t byte_size;
    uint32_t byte_order;
    uint32_t source_kind;
    uint32_t available;
    uint32_t reserved[3];
} PCGXTextureRawTlutResource;

typedef struct PCGXTextureRawState {
    uint32_t owner_epoch;
    uint8_t known_map_mask;
    uint8_t invalid_map_mask;
    uint8_t present_map_mask;
    uint8_t available_map_mask;
    uint16_t known_tlut_mask;
    uint16_t invalid_tlut_mask;
    uint16_t present_tlut_mask;
    uint16_t available_tlut_mask;
    uint32_t invalid;
    uint32_t reserved;
    PCGXTextureRawMapRecord maps[PC_GX_TEXTURE_RAW_MAP_COUNT];
    PCGXTextureRawImageResource images[PC_GX_TEXTURE_RAW_MAP_COUNT];
    PCGXTextureRawTlutResource tluts[PC_GX_TEXTURE_RAW_TLUT_COUNT];
} PCGXTextureRawState;

/* This is the pointer-bearing sideband, not part of PCGXTextureRawState. */
typedef struct PCGXTextureBorrowedResource {
    const void* bytes;
    uint32_t byte_size;
    uint32_t owner_epoch;
    uint64_t generation;
    uint32_t format;
    uint32_t byte_order;
    uint32_t source_kind;
    uint32_t element_count;
} PCGXTextureBorrowedResource;

typedef struct PCGXTextureDynamicLease {
    uint32_t owner_epoch;
    uint8_t image_mask;
    uint16_t tlut_mask;
    uint8_t reserved0;
    uint8_t reserved1;
    PCGXTextureBorrowedResource images[PC_GX_TEXTURE_RAW_MAP_COUNT];
    PCGXTextureBorrowedResource tluts[PC_GX_TEXTURE_RAW_TLUT_COUNT];
} PCGXTextureDynamicLease;

typedef void (*PCGXTextureDynamicSnapshotCallback)(
    void* context,
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic,
    const PCGXTextureDynamicLease* lease
);

void pc_gx_texture_raw_initialize(void);
void pc_gx_texture_raw_shutdown(void);
void pc_gx_texture_raw_mark_global_invalid(void);

void pc_gx_texture_raw_drop_image_lease(unsigned int map);
void pc_gx_texture_raw_drop_all_image_leases(void);
void pc_gx_texture_raw_drop_all_tlut_leases(void);

/* object_words is a GXTexObj-shaped fixed-width word array owned by the PC
 * ABI.  The raw state reads only value words and never stores the object or
 * its image pointer. */
void pc_gx_texture_raw_load_map(
    unsigned int map,
    const uint32_t* object_words,
    uint32_t source_kind,
    uint32_t object_handle_valid
);
void pc_gx_texture_raw_mark_map_invalid(unsigned int map);
void pc_gx_texture_raw_publish_image_lease(
    unsigned int map,
    const void* bytes
);

void pc_gx_texture_raw_load_tlut(
    unsigned int slot,
    uint32_t format,
    uint32_t entry_count,
    uint32_t byte_order,
    uint32_t source_kind
);
void pc_gx_texture_raw_mark_tlut_invalid(unsigned int slot);
void pc_gx_texture_raw_publish_tlut_lease(
    unsigned int slot,
    const void* bytes
);
void pc_gx_texture_raw_set_tlut_native_le(unsigned int slot);

void pc_gx_texture_raw_snapshot(PCGXTextureRawState* destination);
int pc_gx_texture_raw_get_image_lease(
    unsigned int map,
    PCGXTextureBorrowedResource* destination
);
int pc_gx_texture_raw_get_tlut_lease(
    unsigned int slot,
    PCGXTextureBorrowedResource* destination
);

/* The converted-image marker is one-shot and is consumed by GXLoadTexObj. */
void pc_gx_texture_mark_image_converted(unsigned int map);
void pc_gx_texture_clear_image_source_markers(void);
uint32_t pc_gx_texture_consume_image_source_kind(unsigned int map);

int pc_gx_build_texture_dynamic_snapshot(
    AcgcGxCanonicalTextureState* texture_destination,
    AcgcGxCanonicalDynamicState* dynamic_destination,
    PCGXTextureDynamicLease* lease_destination
);

void pc_gx_set_texture_dynamic_snapshot_callback(
    PCGXTextureDynamicSnapshotCallback callback,
    void* context
);
void pc_gx_clear_texture_dynamic_snapshot_callback(void);
int pc_gx_try_texture_dynamic_snapshot(void);

#ifdef __cplusplus
}
#endif

#endif
