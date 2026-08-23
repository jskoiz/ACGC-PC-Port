#include "pc_gx_internal.h"
#include "pc_gx_texture_raw_state.h"

#include <string.h>

static PCGXTextureDynamicSnapshotCallback s_texture_snapshot_callback;
static void* s_texture_snapshot_context;

static uint32_t snapshot_popcount32(uint32_t value) {
    uint32_t count = 0;

    while (value != 0) {
        count += value & UINT32_C(1);
        value >>= 1;
    }
    return count;
}

static int snapshot_format_uses_tlut(uint32_t format) {
    return format == ACGC_GX_CANONICAL_TEXTURE_FORMAT_C4 ||
        format == ACGC_GX_CANONICAL_TEXTURE_FORMAT_C8 ||
        format == ACGC_GX_CANONICAL_TEXTURE_FORMAT_C14X2;
}

static void snapshot_split_generation(
    uint64_t generation,
    uint32_t* low,
    uint32_t* high
) {
    if (low != NULL) {
        *low = (uint32_t)generation;
    }
    if (high != NULL) {
        *high = (uint32_t)(generation >> 32);
    }
}

static int snapshot_image_lease_matches(
    unsigned int map,
    const PCGXTextureRawImageResource* image,
    PCGXTextureBorrowedResource* destination
) {
    PCGXTextureBorrowedResource lease;

    if (image == NULL ||
        !pc_gx_texture_raw_get_image_lease(map, &lease) ||
        lease.bytes == NULL || lease.byte_size == 0 ||
        lease.owner_epoch != image->owner_epoch ||
        lease.generation != image->generation ||
        lease.byte_size != image->byte_size ||
        lease.format != image->format ||
        lease.byte_order != image->byte_order ||
        lease.source_kind != image->source_kind ||
        ((uintptr_t)lease.bytes & 0x1Fu) != 0) {
        return 0;
    }
    if (destination != NULL) {
        *destination = lease;
    }
    return 1;
}

static int snapshot_tlut_lease_matches(
    unsigned int slot,
    const PCGXTextureRawTlutResource* tlut,
    PCGXTextureBorrowedResource* destination
) {
    PCGXTextureBorrowedResource lease;

    if (tlut == NULL ||
        !pc_gx_texture_raw_get_tlut_lease(slot, &lease) ||
        lease.bytes == NULL || lease.byte_size == 0 ||
        lease.owner_epoch != tlut->owner_epoch ||
        lease.generation != tlut->generation ||
        lease.byte_size != tlut->byte_size ||
        lease.format != tlut->format ||
        lease.byte_order != tlut->byte_order ||
        lease.source_kind != tlut->source_kind ||
        lease.element_count != tlut->entry_count ||
        ((uintptr_t)lease.bytes & 0x1Fu) != 0) {
        return 0;
    }
    if (destination != NULL) {
        *destination = lease;
    }
    return 1;
}

static int snapshot_raw_state_is_complete(
    const PCGXTextureRawState* raw
) {
    uint32_t map;
    uint32_t slot;

    if (raw == NULL || raw->owner_epoch == 0 || raw->invalid != 0 ||
        raw->invalid_map_mask != 0 || raw->invalid_tlut_mask != 0 ||
        (raw->known_map_mask & (uint8_t)~raw->present_map_mask) != 0 ||
        (raw->known_tlut_mask & (uint16_t)~raw->present_tlut_mask) != 0 ||
        (raw->available_map_mask & (uint8_t)~raw->present_map_mask) != 0 ||
        (raw->available_tlut_mask & (uint16_t)~raw->present_tlut_mask) != 0) {
        return 0;
    }
    for (map = 0; map < PC_GX_TEXTURE_RAW_MAP_COUNT; map++) {
        const uint8_t mask = (uint8_t)(UINT8_C(1) << map);
        const PCGXTextureRawMapRecord* map_record = &raw->maps[map];
        const PCGXTextureRawImageResource* image = &raw->images[map];
        const int known = (raw->known_map_mask & mask) != 0;

        if (!known) {
            if (map_record->known != 0 || map_record->invalid != 0 ||
                image->logical_id != 0 || image->owner_epoch != 0 ||
                image->generation != 0 || image->available != 0) {
                return 0;
            }
            continue;
        }
        if (map_record->logical_id != UINT32_C(1) + map ||
            map_record->image_resource_id != map_record->logical_id ||
            map_record->known != 1 || map_record->invalid != 0 ||
            image->logical_id != map_record->image_resource_id ||
            image->owner_epoch != raw->owner_epoch || image->generation == 0 ||
            image->byte_size == 0 ||
            (map_record->indexed != 0) !=
                snapshot_format_uses_tlut(image->format)) {
            return 0;
        }
        if (map_record->indexed != 0) {
            if (map_record->tlut_name >= PC_GX_TEXTURE_RAW_TLUT_COUNT ||
                map_record->tlut_resource_id !=
                    ACGC_GX_CANONICAL_TEXTURE_TLUT_RESOURCE_ID_BASE +
                        map_record->tlut_name) {
                return 0;
            }
            slot = map_record->tlut_name;
            if ((raw->known_tlut_mask & (uint16_t)(UINT16_C(1) << slot)) == 0) {
                return 0;
            }
        } else if (map_record->tlut_name != UINT32_MAX ||
                   map_record->tlut_resource_id != 0) {
            return 0;
        }
    }
    for (slot = 0; slot < PC_GX_TEXTURE_RAW_TLUT_COUNT; slot++) {
        const uint16_t mask = (uint16_t)(UINT16_C(1) << slot);
        const PCGXTextureRawTlutResource* tlut = &raw->tluts[slot];
        if ((raw->present_tlut_mask & mask) == 0) {
            if (tlut->logical_id != 0 || tlut->owner_epoch != 0 ||
                tlut->generation != 0 || tlut->available != 0) {
                return 0;
            }
            continue;
        }
        if ((raw->known_tlut_mask & mask) == 0 ||
            tlut->logical_id !=
                ACGC_GX_CANONICAL_TEXTURE_TLUT_RESOURCE_ID_BASE + slot ||
            tlut->owner_epoch != raw->owner_epoch || tlut->generation == 0 ||
            tlut->owner_slot != slot || tlut->format > 2 ||
            tlut->entry_count == 0 || tlut->entry_count > 0x4000 ||
            tlut->byte_size != tlut->entry_count * 2u) {
            return 0;
        }
    }
    return 1;
}

static int snapshot_build_texture(
    const PCGXTextureRawState* raw,
    AcgcGxCanonicalTextureState* texture,
    uint32_t* required_tlut_mask
) {
    uint32_t map;

    if (raw == NULL || texture == NULL || required_tlut_mask == NULL ||
        !snapshot_raw_state_is_complete(raw)) {
        return 0;
    }
    memset(texture, 0, sizeof(*texture));
    texture->header.known_map_mask = raw->known_map_mask;
    texture->header.known_map_count =
        snapshot_popcount32(raw->known_map_mask);
    texture->header.required_map_mask = raw->known_map_mask;
    texture->header.record_byte_offset =
        ACGC_GX_CANONICAL_TEXTURE_HEADER_BYTE_SIZE;
    texture->header.record_count = ACGC_GX_CANONICAL_TEXTURE_STATE_COUNT;
    texture->header.record_capacity = ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY;
    texture->header.record_word_count =
        ACGC_GX_CANONICAL_TEXTURE_RECORD_WORD_COUNT;
    texture->header.resource_id_scheme =
        ACGC_GX_CANONICAL_TEXTURE_RESOURCE_ID_SCHEME;
    *required_tlut_mask = 0;

    for (map = 0; map < PC_GX_TEXTURE_RAW_MAP_COUNT; map++) {
        const uint32_t map_mask = UINT32_C(1) << map;
        const PCGXTextureRawMapRecord* map_record = &raw->maps[map];
        const PCGXTextureRawImageResource* image = &raw->images[map];
        AcgcGxCanonicalTextureRecord* record = &texture->records[map];
        uint32_t flags;

        if ((raw->known_map_mask & map_mask) == 0) {
            continue;
        }
        flags = ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED;
        if (map_record->indexed != 0) {
            flags |= ACGC_GX_CANONICAL_TEXTURE_FLAG_INDEXED |
                ACGC_GX_CANONICAL_TEXTURE_FLAG_TLUT_PRESENT;
            *required_tlut_mask |= UINT32_C(1) << map_record->tlut_name;
            texture->header.indexed_map_mask |= map_mask;
            texture->header.tlut_present_map_mask |= map_mask;
        }
        if (map_record->mipmap != 0) {
            flags |= ACGC_GX_CANONICAL_TEXTURE_FLAG_MIPMAP;
            texture->header.mipmap_map_mask |= map_mask;
        }
        record->flags = flags;
        record->image_resource_id = image->logical_id;
        record->image_owner_epoch = image->owner_epoch;
        snapshot_split_generation(
            image->generation,
            &record->image_generation_lo,
            &record->image_generation_hi
        );
        record->width = image->width;
        record->height = image->height;
        record->image_format = image->format;
        record->wrap_s = image->wrap_s;
        record->wrap_t = image->wrap_t;
        record->min_filter = image->min_filter;
        record->mag_filter = image->mag_filter;
        record->min_lod_q4 = image->min_lod_q4;
        record->max_lod_q4 = image->max_lod_q4;
        record->lod_bias_q5 = image->lod_bias_q5;
        record->bias_clamp = image->bias_clamp;
        record->edge_lod = image->edge_lod;
        record->max_anisotropy = image->max_anisotropy;
        record->mip_level_count = image->mip_level_count;
        record->image_byte_size = image->byte_size;
        record->image_byte_order = image->byte_order;
        record->image_source_kind = image->source_kind;
        if (map_record->indexed != 0) {
            const PCGXTextureRawTlutResource* tlut =
                &raw->tluts[map_record->tlut_name];
            record->tlut_resource_id = tlut->logical_id;
            record->tlut_owner_epoch = tlut->owner_epoch;
            snapshot_split_generation(
                tlut->generation,
                &record->tlut_generation_lo,
                &record->tlut_generation_hi
            );
            record->tlut_name = map_record->tlut_name;
            record->tlut_format = tlut->format;
            record->tlut_entry_count = tlut->entry_count;
            record->tlut_byte_size = tlut->byte_size;
            record->tlut_byte_order = tlut->byte_order;
            record->tlut_source_kind = tlut->source_kind;
        } else {
            record->tlut_name = ACGC_GX_CANONICAL_TEXTURE_TLUT_NAME_NONE;
        }
    }
    return acgc_gx_canonical_texture_state_validate(texture);
}

static void snapshot_fill_dynamic_header(
    const PCGXTextureRawState* raw,
    const AcgcGxCanonicalTextureState* texture,
    uint32_t required_tlut_mask,
    AcgcGxCanonicalDynamicState* dynamic
) {
    memset(dynamic, 0, sizeof(*dynamic));
    dynamic->header.owner_epoch = raw->owner_epoch;
    dynamic->header.present_image_mask = raw->present_map_mask;
    dynamic->header.present_tlut_mask = raw->present_tlut_mask;
    dynamic->header.required_image_mask = texture->header.required_map_mask;
    dynamic->header.required_tlut_mask = required_tlut_mask;
    dynamic->header.present_resource_count =
        snapshot_popcount32(dynamic->header.present_image_mask) +
        snapshot_popcount32(dynamic->header.present_tlut_mask);
    dynamic->header.record_byte_offset =
        ACGC_GX_CANONICAL_DYNAMIC_HEADER_BYTE_SIZE;
    dynamic->header.record_count = ACGC_GX_CANONICAL_DYNAMIC_STATE_COUNT;
    dynamic->header.record_capacity =
        ACGC_GX_CANONICAL_DYNAMIC_STATE_CAPACITY;
    dynamic->header.record_word_count =
        ACGC_GX_CANONICAL_DYNAMIC_RECORD_WORD_COUNT;
    dynamic->header.resource_id_scheme =
        ACGC_GX_CANONICAL_DYNAMIC_RESOURCE_ID_SCHEME;
}

static void snapshot_fill_dynamic_records(
    const PCGXTextureRawState* raw,
    AcgcGxCanonicalDynamicState* dynamic
) {
    uint32_t map;
    uint32_t slot;

    for (map = 0; map < PC_GX_TEXTURE_RAW_MAP_COUNT; map++) {
        const uint32_t map_mask = UINT32_C(1) << map;
        const PCGXTextureRawImageResource* image = &raw->images[map];
        AcgcGxCanonicalDynamicRecord* record = &dynamic->records[map];
        PCGXTextureBorrowedResource lease;

        if ((raw->present_map_mask & map_mask) == 0) {
            continue;
        }
        record->resource_id = image->logical_id;
        record->kind = ACGC_GX_CANONICAL_DYNAMIC_KIND_IMAGE;
        record->owner_epoch = image->owner_epoch;
        snapshot_split_generation(
            image->generation,
            &record->generation_lo,
            &record->generation_hi
        );
        record->owner_slot = map;
        record->byte_size = image->byte_size;
        record->byte_order = image->byte_order;
        record->alignment = ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES;
        record->source_kind = image->source_kind;
        record->format = image->format;
        if (snapshot_image_lease_matches(map, image, &lease)) {
            record->byte_flags =
                ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
                ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED;
        }
    }
    for (slot = 0; slot < PC_GX_TEXTURE_RAW_TLUT_COUNT; slot++) {
        const uint32_t record_index =
            ACGC_GX_CANONICAL_DYNAMIC_IMAGE_MAP_COUNT + slot;
        const uint16_t slot_mask = (uint16_t)(UINT16_C(1) << slot);
        const PCGXTextureRawTlutResource* tlut = &raw->tluts[slot];
        AcgcGxCanonicalDynamicRecord* record = &dynamic->records[record_index];
        PCGXTextureBorrowedResource lease;

        if ((raw->present_tlut_mask & slot_mask) == 0) {
            continue;
        }
        record->resource_id = tlut->logical_id;
        record->kind = ACGC_GX_CANONICAL_DYNAMIC_KIND_TLUT;
        record->owner_epoch = tlut->owner_epoch;
        snapshot_split_generation(
            tlut->generation,
            &record->generation_lo,
            &record->generation_hi
        );
        record->owner_slot = slot;
        record->byte_size = tlut->byte_size;
        record->byte_order = tlut->byte_order;
        record->alignment = ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES;
        record->source_kind = tlut->source_kind;
        record->format = tlut->format;
        record->element_count = tlut->entry_count;
        if (snapshot_tlut_lease_matches(slot, tlut, &lease)) {
            record->byte_flags =
                ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE |
                ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED;
        }
    }
}

static void snapshot_fill_lease(
    const PCGXTextureRawState* raw,
    PCGXTextureDynamicLease* lease
) {
    uint32_t map;
    uint32_t slot;

    memset(lease, 0, sizeof(*lease));
    lease->owner_epoch = raw->owner_epoch;
    for (map = 0; map < PC_GX_TEXTURE_RAW_MAP_COUNT; map++) {
        if ((raw->available_map_mask & (uint8_t)(UINT8_C(1) << map)) != 0 &&
            pc_gx_texture_raw_get_image_lease(map, &lease->images[map])) {
            lease->image_mask |= (uint8_t)(UINT8_C(1) << map);
        }
    }
    for (slot = 0; slot < PC_GX_TEXTURE_RAW_TLUT_COUNT; slot++) {
        if ((raw->available_tlut_mask & (uint16_t)(UINT16_C(1) << slot)) != 0 &&
            pc_gx_texture_raw_get_tlut_lease(slot, &lease->tluts[slot])) {
            lease->tlut_mask |= (uint16_t)(UINT16_C(1) << slot);
        }
    }
}

static int snapshot_build_while_borrowed(
    const PCGXTextureRawBorrow* borrow,
    AcgcGxCanonicalTextureState* texture_destination,
    AcgcGxCanonicalDynamicState* dynamic_destination,
    PCGXTextureRawState* raw_capture_destination,
    PCGXTextureDynamicLease* lease_destination
) {
    PCGXTextureRawState raw;
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalDynamicState dynamic;
    PCGXTextureDynamicLease lease;
    uint32_t required_tlut_mask;

    if (borrow == NULL || texture_destination == NULL ||
        dynamic_destination == NULL || lease_destination == NULL ||
        raw_capture_destination == NULL) {
        return 0;
    }
    pc_gx_texture_raw_snapshot(&raw);
    if (!snapshot_build_texture(&raw, &texture, &required_tlut_mask)) {
        return 0;
    }
    snapshot_fill_dynamic_header(
        &raw, &texture, required_tlut_mask, &dynamic
    );
    snapshot_fill_dynamic_records(&raw, &dynamic);
    snapshot_fill_lease(&raw, &lease);
    if (!acgc_gx_canonical_dynamic_state_validate(&dynamic) ||
        !acgc_gx_canonical_texture_dynamic_validate(&texture, &dynamic)) {
        return 0;
    }
    if (!pc_gx_texture_raw_revalidate_borrow(borrow, &raw, &lease)) {
        return 0;
    }
    *texture_destination = texture;
    *dynamic_destination = dynamic;
    *raw_capture_destination = raw;
    *lease_destination = lease;
    return 1;
}

int pc_gx_build_texture_dynamic_snapshot_borrowed(
    const PCGXTextureRawBorrow* borrow,
    AcgcGxCanonicalTextureState* texture_destination,
    AcgcGxCanonicalDynamicState* dynamic_destination,
    PCGXTextureRawState* raw_capture_destination,
    PCGXTextureDynamicLease* lease_destination
) {
    return snapshot_build_while_borrowed(
        borrow, texture_destination, dynamic_destination,
        raw_capture_destination, lease_destination
    );
}

void pc_gx_set_texture_dynamic_snapshot_callback(
    PCGXTextureDynamicSnapshotCallback callback,
    void* context
) {
    if (pc_gx_texture_raw_borrow_is_active()) {
        return;
    }
    s_texture_snapshot_callback = callback;
    s_texture_snapshot_context = callback != NULL ? context : NULL;
}

void pc_gx_clear_texture_dynamic_snapshot_callback(void) {
    if (pc_gx_texture_raw_borrow_is_active()) {
        return;
    }
    s_texture_snapshot_callback = NULL;
    s_texture_snapshot_context = NULL;
}

int pc_gx_try_texture_dynamic_snapshot(void) {
    PCGXTextureDynamicSnapshotCallback callback;
    void* context;
    PCGXTextureRawBorrow borrow = {0};
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalDynamicState dynamic;
    PCGXTextureDynamicLease lease;
    PCGXTextureRawState raw_capture;

    callback = s_texture_snapshot_callback;
    context = s_texture_snapshot_context;
    if (callback == NULL || !pc_gx_texture_raw_begin_borrow(&borrow)) {
        return 0;
    }

    if (!pc_gx_build_texture_dynamic_snapshot_borrowed(
            &borrow, &texture, &dynamic, &raw_capture, &lease)) {
        (void)pc_gx_texture_raw_end_borrow(&borrow);
        return 0;
    }

    /* Keep the caller-owned borrow active for the entire synchronous callback.
     * The callback receives no token, so it cannot release this borrow; raw
     * writers and the known GXCopyTex write path fail closed instead of
     * changing the lease behind the callback's read-only borrowed pointers.
     */
    callback(context, &texture, &dynamic, &lease);
    /* Supported single-threaded guarded APIs keep this transaction stable.
     * Arbitrary direct writes and concurrent mutation are out of contract; if
     * revalidation fails after the callback, its completed side effects cannot
     * be undone. */
    if (!pc_gx_texture_raw_revalidate_borrow(&borrow, &raw_capture, &lease)) {
        (void)pc_gx_texture_raw_end_borrow(&borrow);
        return 0;
    }
    return pc_gx_texture_raw_end_borrow(&borrow);
}
