#include "acgc/gx_canonical_texture_state.h"

#include <string.h>

static uint32_t canonical_texture_popcount(uint32_t value) {
    uint32_t count = 0;

    while (value != 0) {
        count += value & UINT32_C(1);
        value >>= 1;
    }
    return count;
}

static int canonical_texture_image_format_is_valid(uint32_t format) {
    switch (format) {
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_I4:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_I8:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_IA4:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_IA8:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_RGB565:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_RGB5A3:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_RGBA8:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_C4:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_C8:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_C14X2:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_CMPR:
            return 1;
        default:
            return 0;
    }
}

static int canonical_texture_tlut_format_is_valid(uint32_t format) {
    return format == ACGC_GX_CANONICAL_TEXTURE_TLUT_FORMAT_IA8 ||
        format == ACGC_GX_CANONICAL_TEXTURE_TLUT_FORMAT_RGB565 ||
        format == ACGC_GX_CANONICAL_TEXTURE_TLUT_FORMAT_RGB5A3;
}

static int canonical_texture_source_kind_is_valid(uint32_t source_kind) {
    return source_kind ==
            ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_RAW_GUEST ||
        source_kind ==
            ACGC_GX_CANONICAL_TEXTURE_SOURCE_KIND_EMU64_CONVERTED;
}

static int canonical_texture_byte_order_is_valid(uint32_t byte_order) {
    return byte_order == ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_GX_BE ||
        byte_order == ACGC_GX_CANONICAL_TEXTURE_BYTE_ORDER_LE;
}

static int canonical_texture_reserved_words_are_zero(
    const uint32_t* words,
    uint32_t count
) {
    uint32_t index;

    if (words == NULL) {
        return 0;
    }
    for (index = 0; index < count; index++) {
        if (words[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int canonical_texture_signed_q5_is_valid(uint32_t raw_value) {
    const int64_t value = raw_value <= UINT32_C(0x7FFFFFFF) ?
        (int64_t)raw_value :
        (int64_t)raw_value - INT64_C(4294967296);

    return value >= ACGC_GX_CANONICAL_TEXTURE_LOD_BIAS_Q5_MIN &&
        value <= ACGC_GX_CANONICAL_TEXTURE_LOD_BIAS_Q5_MAX;
}

static int canonical_texture_tile_geometry(
    uint32_t format,
    uint32_t* tile_width,
    uint32_t* tile_height,
    uint32_t* tile_byte_size
) {
    if (tile_width == NULL || tile_height == NULL ||
        tile_byte_size == NULL) {
        return 0;
    }

    switch (format) {
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_I4:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_C4:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_CMPR:
            *tile_width = 8;
            *tile_height = 8;
            *tile_byte_size = 32;
            return 1;
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_I8:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_IA4:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_C8:
            *tile_width = 8;
            *tile_height = 4;
            *tile_byte_size = 32;
            return 1;
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_IA8:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_RGB565:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_RGB5A3:
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_C14X2:
            *tile_width = 4;
            *tile_height = 4;
            *tile_byte_size = 32;
            return 1;
        case ACGC_GX_CANONICAL_TEXTURE_FORMAT_RGBA8:
            *tile_width = 4;
            *tile_height = 4;
            *tile_byte_size = 64;
            return 1;
        default:
            return 0;
    }
}

static uint32_t canonical_texture_max_mip_level_count(
    uint32_t width,
    uint32_t height
) {
    uint32_t dimension = width > height ? width : height;
    uint32_t count = 1;

    while (dimension > 1) {
        dimension >>= 1;
        count++;
    }
    return count;
}

static int canonical_texture_image_byte_size(
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t mip_level_count,
    uint32_t* byte_size
) {
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t tile_byte_size;
    uint32_t level_width = width;
    uint32_t level_height = height;
    uint32_t level;
    uint64_t total = 0;

    if (byte_size == NULL ||
        width < ACGC_GX_CANONICAL_TEXTURE_DIMENSION_MIN ||
        width > ACGC_GX_CANONICAL_TEXTURE_DIMENSION_MAX ||
        height < ACGC_GX_CANONICAL_TEXTURE_DIMENSION_MIN ||
        height > ACGC_GX_CANONICAL_TEXTURE_DIMENSION_MAX ||
        mip_level_count == 0 ||
        mip_level_count > ACGC_GX_CANONICAL_TEXTURE_MIP_LEVEL_COUNT_MAX ||
        !canonical_texture_tile_geometry(
            format, &tile_width, &tile_height, &tile_byte_size)) {
        return 0;
    }

    for (level = 0; level < mip_level_count; level++) {
        const uint64_t tiles_x =
            ((uint64_t)level_width + tile_width - 1) / tile_width;
        const uint64_t tiles_y =
            ((uint64_t)level_height + tile_height - 1) / tile_height;
        const uint64_t level_size = tiles_x * tiles_y * tile_byte_size;

        if (total > UINT64_MAX - level_size) {
            return 0;
        }
        total += level_size;
        if (level_width > 1) {
            level_width >>= 1;
        }
        if (level_height > 1) {
            level_height >>= 1;
        }
    }

    if (total == 0 || total > UINT32_MAX) {
        return 0;
    }
    *byte_size = (uint32_t)total;
    return 1;
}

static int canonical_texture_header_is_valid(
    const AcgcGxCanonicalTextureHeader* header
) {
    if (header == NULL ||
        (header->known_map_mask & ~UINT32_C(0x000000FF)) != 0 ||
        header->known_map_count !=
            canonical_texture_popcount(header->known_map_mask) ||
        (header->indexed_map_mask & ~header->known_map_mask) != 0 ||
        (header->mipmap_map_mask & ~header->known_map_mask) != 0 ||
        (header->tlut_present_map_mask & ~header->known_map_mask) != 0 ||
        (header->required_map_mask & ~header->known_map_mask) != 0 ||
        header->tlut_present_map_mask != header->indexed_map_mask ||
        header->record_byte_offset !=
            ACGC_GX_CANONICAL_TEXTURE_HEADER_BYTE_SIZE ||
        header->record_count != ACGC_GX_CANONICAL_TEXTURE_STATE_COUNT ||
        header->record_capacity != ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY ||
        header->record_word_count !=
            ACGC_GX_CANONICAL_TEXTURE_RECORD_WORD_COUNT ||
        header->resource_id_scheme !=
            ACGC_GX_CANONICAL_TEXTURE_RESOURCE_ID_SCHEME ||
        !canonical_texture_reserved_words_are_zero(header->reserved, 5)) {
        return 0;
    }
    return 1;
}

static int canonical_texture_record_is_zero(
    const AcgcGxCanonicalTextureRecord* record
) {
    return record != NULL &&
        record->flags == 0 &&
        record->image_resource_id == 0 &&
        record->image_owner_epoch == 0 &&
        record->image_generation_lo == 0 &&
        record->image_generation_hi == 0 &&
        record->width == 0 &&
        record->height == 0 &&
        record->image_format == 0 &&
        record->wrap_s == 0 &&
        record->wrap_t == 0 &&
        record->min_filter == 0 &&
        record->mag_filter == 0 &&
        record->min_lod_q4 == 0 &&
        record->max_lod_q4 == 0 &&
        record->lod_bias_q5 == 0 &&
        record->bias_clamp == 0 &&
        record->edge_lod == 0 &&
        record->max_anisotropy == 0 &&
        record->mip_level_count == 0 &&
        record->image_byte_size == 0 &&
        record->image_byte_order == 0 &&
        record->image_source_kind == 0 &&
        record->tlut_resource_id == 0 &&
        record->tlut_owner_epoch == 0 &&
        record->tlut_generation_lo == 0 &&
        record->tlut_generation_hi == 0 &&
        record->tlut_name == 0 &&
        record->tlut_format == 0 &&
        record->tlut_entry_count == 0 &&
        record->tlut_byte_size == 0 &&
        record->tlut_byte_order == 0 &&
        record->tlut_source_kind == 0 &&
        canonical_texture_reserved_words_are_zero(record->reserved, 4);
}

static int canonical_texture_record_is_valid(
    const AcgcGxCanonicalTextureRecord* record,
    uint32_t map
) {
    uint32_t indexed;
    uint32_t mipmap;
    uint32_t expected_flags;
    uint32_t expected_image_byte_size;
    uint32_t max_mip_level_count;

    if (record == NULL) {
        return 0;
    }

    indexed =
        (record->flags & ACGC_GX_CANONICAL_TEXTURE_FLAG_INDEXED) != 0;
    mipmap =
        (record->flags & ACGC_GX_CANONICAL_TEXTURE_FLAG_MIPMAP) != 0;
    expected_flags =
        (indexed ? ACGC_GX_CANONICAL_TEXTURE_FLAG_INDEXED : 0) |
        (mipmap ? ACGC_GX_CANONICAL_TEXTURE_FLAG_MIPMAP : 0) |
        (indexed ? ACGC_GX_CANONICAL_TEXTURE_FLAG_TLUT_PRESENT : 0) |
        ((record->flags & ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED) !=
            0 ? ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED : 0);
    max_mip_level_count = canonical_texture_max_mip_level_count(
        record->width, record->height);

    if ((record->flags & ~ACGC_GX_CANONICAL_TEXTURE_FLAG_MASK) != 0 ||
        record->flags != expected_flags ||
        record->image_resource_id !=
            ACGC_GX_CANONICAL_TEXTURE_IMAGE_RESOURCE_ID_BASE + map ||
        record->image_owner_epoch == 0 ||
        (record->image_generation_lo == 0 &&
            record->image_generation_hi == 0) ||
        record->width < ACGC_GX_CANONICAL_TEXTURE_DIMENSION_MIN ||
        record->width > ACGC_GX_CANONICAL_TEXTURE_DIMENSION_MAX ||
        record->height < ACGC_GX_CANONICAL_TEXTURE_DIMENSION_MIN ||
        record->height > ACGC_GX_CANONICAL_TEXTURE_DIMENSION_MAX ||
        !canonical_texture_image_format_is_valid(record->image_format) ||
        record->wrap_s > ACGC_GX_CANONICAL_TEXTURE_WRAP_MIRROR ||
        record->wrap_t > ACGC_GX_CANONICAL_TEXTURE_WRAP_MIRROR ||
        record->min_filter > ACGC_GX_CANONICAL_TEXTURE_MIN_FILTER_MAX ||
        record->mag_filter > ACGC_GX_CANONICAL_TEXTURE_MAG_FILTER_MAX ||
        record->min_lod_q4 > ACGC_GX_CANONICAL_TEXTURE_LOD_Q4_MAX ||
        record->max_lod_q4 > ACGC_GX_CANONICAL_TEXTURE_LOD_Q4_MAX ||
        record->min_lod_q4 > record->max_lod_q4 ||
        !canonical_texture_signed_q5_is_valid(record->lod_bias_q5) ||
        record->bias_clamp > 1 || record->edge_lod > 1 ||
        record->max_anisotropy > ACGC_GX_CANONICAL_TEXTURE_ANISOTROPY_MAX ||
        record->mip_level_count == 0 ||
        record->mip_level_count > max_mip_level_count ||
        (!mipmap && record->mip_level_count != 1) ||
        !canonical_texture_byte_order_is_valid(record->image_byte_order) ||
        !canonical_texture_source_kind_is_valid(record->image_source_kind) ||
        !canonical_texture_reserved_words_are_zero(record->reserved, 4) ||
        !canonical_texture_image_byte_size(
            record->image_format,
            record->width,
            record->height,
            record->mip_level_count,
            &expected_image_byte_size) ||
        record->image_byte_size != expected_image_byte_size) {
        return 0;
    }

    if (!indexed) {
        return record->tlut_resource_id == 0 &&
            record->tlut_owner_epoch == 0 &&
            record->tlut_generation_lo == 0 &&
            record->tlut_generation_hi == 0 &&
            record->tlut_name == ACGC_GX_CANONICAL_TEXTURE_TLUT_NAME_NONE &&
            record->tlut_format == 0 &&
            record->tlut_entry_count == 0 &&
            record->tlut_byte_size == 0 &&
            record->tlut_byte_order == 0 &&
            record->tlut_source_kind == 0;
    }

    return record->tlut_name <= ACGC_GX_CANONICAL_TEXTURE_TLUT_NAME_MAX &&
        record->tlut_resource_id ==
            ACGC_GX_CANONICAL_TEXTURE_TLUT_RESOURCE_ID_BASE +
                record->tlut_name &&
        record->tlut_owner_epoch != 0 &&
        (record->tlut_generation_lo != 0 ||
            record->tlut_generation_hi != 0) &&
        canonical_texture_tlut_format_is_valid(record->tlut_format) &&
        record->tlut_entry_count != 0 &&
        record->tlut_entry_count <=
            ACGC_GX_CANONICAL_TEXTURE_TLUT_ENTRY_MAX &&
        (uint64_t)record->tlut_entry_count * UINT64_C(2) ==
            (uint64_t)record->tlut_byte_size &&
        canonical_texture_byte_order_is_valid(record->tlut_byte_order) &&
        canonical_texture_source_kind_is_valid(record->tlut_source_kind);
}

int acgc_gx_canonical_texture_state_validate(
    const AcgcGxCanonicalTextureState* state
) {
    uint32_t map;

    if (state == NULL || !canonical_texture_header_is_valid(&state->header)) {
        return 0;
    }

    for (map = 0; map < ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY; map++) {
        const uint32_t map_mask = UINT32_C(1) << map;
        const AcgcGxCanonicalTextureRecord* record = &state->records[map];
        const int known = (state->header.known_map_mask & map_mask) != 0;

        if (!known) {
            if (!canonical_texture_record_is_zero(record)) {
                return 0;
            }
            continue;
        }

        if (!canonical_texture_record_is_valid(record, map) ||
            (((record->flags & ACGC_GX_CANONICAL_TEXTURE_FLAG_INDEXED) != 0) !=
                ((state->header.indexed_map_mask & map_mask) != 0)) ||
            (((record->flags & ACGC_GX_CANONICAL_TEXTURE_FLAG_MIPMAP) != 0) !=
                ((state->header.mipmap_map_mask & map_mask) != 0)) ||
            (((record->flags &
                ACGC_GX_CANONICAL_TEXTURE_FLAG_TLUT_PRESENT) != 0) !=
                ((state->header.tlut_present_map_mask & map_mask) != 0)) ||
            (((record->flags &
                ACGC_GX_CANONICAL_TEXTURE_FLAG_RESOURCE_REQUIRED) != 0) !=
                ((state->header.required_map_mask & map_mask) != 0))) {
            return 0;
        }
    }
    return 1;
}

static int canonical_texture_entry_is_absent(
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry
) {
    return entry != NULL &&
        entry->section_id == ACGC_GX_CANONICAL_TEXTURE_SECTION_ID &&
        entry->section_version == 0 &&
        entry->byte_offset == 0 &&
        entry->byte_size == 0 &&
        entry->count == 0 &&
        entry->capacity == 0 &&
        entry->valid_mask == 0 &&
        entry->reserved == 0;
}

int acgc_gx_canonical_texture_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
) {
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    if (!acgc_gx_canonical_envelope_validate(
            envelope, envelope_byte_size)) {
        return 0;
    }

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_TEXTURE_SECTION_ID - 1];
    if ((envelope->header.present_state_mask &
            ACGC_GX_CANONICAL_TEXTURE_SECTION_MASK) == 0) {
        return canonical_texture_entry_is_absent(entry);
    }

    return entry->section_id == ACGC_GX_CANONICAL_TEXTURE_SECTION_ID &&
        entry->section_version == ACGC_GX_CANONICAL_TEXTURE_SECTION_VERSION &&
        entry->byte_size == ACGC_GX_CANONICAL_TEXTURE_SECTION_BYTE_SIZE &&
        entry->count == ACGC_GX_CANONICAL_TEXTURE_SECTION_COUNT &&
        entry->capacity == ACGC_GX_CANONICAL_TEXTURE_SECTION_CAPACITY &&
        entry->valid_mask == ACGC_GX_CANONICAL_TEXTURE_SECTION_MASK &&
        entry->reserved == 0;
}

static int canonical_texture_dynamic_bytes_are_available(
    const AcgcGxCanonicalDynamicRecord* record
) {
    return record != NULL &&
        (record->byte_flags & ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE) != 0;
}

static int canonical_texture_dynamic_image_matches(
    const AcgcGxCanonicalTextureRecord* texture,
    const AcgcGxCanonicalDynamicRecord* dynamic
) {
    return texture != NULL && dynamic != NULL &&
        dynamic->resource_id == texture->image_resource_id &&
        dynamic->owner_epoch == texture->image_owner_epoch &&
        dynamic->generation_lo == texture->image_generation_lo &&
        dynamic->generation_hi == texture->image_generation_hi &&
        dynamic->byte_size == texture->image_byte_size &&
        dynamic->byte_order == texture->image_byte_order &&
        dynamic->source_kind == texture->image_source_kind &&
        dynamic->format == texture->image_format;
}

static int canonical_texture_dynamic_tlut_matches(
    const AcgcGxCanonicalTextureRecord* texture,
    const AcgcGxCanonicalDynamicRecord* dynamic
) {
    return texture != NULL && dynamic != NULL &&
        dynamic->resource_id == texture->tlut_resource_id &&
        dynamic->owner_epoch == texture->tlut_owner_epoch &&
        dynamic->generation_lo == texture->tlut_generation_lo &&
        dynamic->generation_hi == texture->tlut_generation_hi &&
        dynamic->byte_size == texture->tlut_byte_size &&
        dynamic->byte_order == texture->tlut_byte_order &&
        dynamic->source_kind == texture->tlut_source_kind &&
        dynamic->format == texture->tlut_format &&
        dynamic->element_count == texture->tlut_entry_count;
}

int acgc_gx_canonical_texture_dynamic_validate(
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalDynamicState* dynamic
) {
    uint32_t expected_required_tlut_mask = 0;
    uint32_t map;

    if (!acgc_gx_canonical_texture_state_validate(texture) ||
        !acgc_gx_canonical_dynamic_state_validate(dynamic)) {
        return 0;
    }

    for (map = 0; map < ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY; map++) {
        const uint32_t map_mask = UINT32_C(1) << map;
        const AcgcGxCanonicalTextureRecord* texture_record =
            &texture->records[map];

        if ((texture->header.required_map_mask & map_mask) != 0 &&
            (texture->header.indexed_map_mask & map_mask) != 0) {
            expected_required_tlut_mask |=
                UINT32_C(1) << texture_record->tlut_name;
        }
    }

    if (dynamic->header.required_image_mask !=
            texture->header.required_map_mask ||
        dynamic->header.required_tlut_mask != expected_required_tlut_mask) {
        return 0;
    }

    for (map = 0; map < ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY; map++) {
        const uint32_t map_mask = UINT32_C(1) << map;
        const AcgcGxCanonicalTextureRecord* texture_record =
            &texture->records[map];
        const AcgcGxCanonicalDynamicRecord* image_record =
            &dynamic->records[map];

        if ((texture->header.known_map_mask & map_mask) == 0) {
            continue;
        }

        if ((dynamic->header.present_image_mask & map_mask) != 0 &&
            !canonical_texture_dynamic_image_matches(
                texture_record, image_record)) {
            return 0;
        }
        if ((texture->header.required_map_mask & map_mask) != 0 &&
            ((dynamic->header.present_image_mask & map_mask) == 0 ||
             !canonical_texture_dynamic_bytes_are_available(image_record))) {
            return 0;
        }

        if ((texture->header.indexed_map_mask & map_mask) != 0) {
            const uint32_t tlut_slot = texture_record->tlut_name;
            const uint32_t tlut_mask = UINT32_C(1) << tlut_slot;
            const AcgcGxCanonicalDynamicRecord* tlut_record =
                &dynamic->records[
                    ACGC_GX_CANONICAL_DYNAMIC_IMAGE_MAP_COUNT + tlut_slot];

            if ((dynamic->header.present_tlut_mask & tlut_mask) != 0 &&
                !canonical_texture_dynamic_tlut_matches(
                    texture_record, tlut_record)) {
                return 0;
            }
            if ((texture->header.required_map_mask & map_mask) != 0 &&
                ((dynamic->header.required_tlut_mask & tlut_mask) == 0 ||
                 (dynamic->header.present_tlut_mask & tlut_mask) == 0 ||
                 !canonical_texture_dynamic_bytes_are_available(
                     tlut_record))) {
                return 0;
            }
        }
    }

    return 1;
}

static void canonical_texture_write_le32(
    uint8_t* destination,
    uint32_t value
) {
    destination[0] = (uint8_t)(value & UINT32_C(0xFF));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    destination[3] = (uint8_t)((value >> 24) & UINT32_C(0xFF));
}

static void canonical_texture_encode_word(
    uint8_t* destination,
    size_t* offset,
    uint32_t value
) {
    canonical_texture_write_le32(destination + *offset, value);
    *offset += sizeof(uint32_t);
}

int acgc_gx_canonical_texture_state_encode(
    const AcgcGxCanonicalTextureState* state,
    uint8_t* destination,
    size_t destination_byte_size
) {
    uint8_t encoded[ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE];
    size_t offset = 0;
    uint32_t index;

    if (state == NULL || destination == NULL ||
        destination_byte_size != ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE ||
        !acgc_gx_canonical_texture_state_validate(state)) {
        return 0;
    }

    memset(encoded, 0, sizeof(encoded));
    canonical_texture_encode_word(
        encoded, &offset, state->header.known_map_mask);
    canonical_texture_encode_word(
        encoded, &offset, state->header.known_map_count);
    canonical_texture_encode_word(
        encoded, &offset, state->header.indexed_map_mask);
    canonical_texture_encode_word(
        encoded, &offset, state->header.mipmap_map_mask);
    canonical_texture_encode_word(
        encoded, &offset, state->header.tlut_present_map_mask);
    canonical_texture_encode_word(
        encoded, &offset, state->header.required_map_mask);
    canonical_texture_encode_word(
        encoded, &offset, state->header.record_byte_offset);
    canonical_texture_encode_word(
        encoded, &offset, state->header.record_count);
    canonical_texture_encode_word(
        encoded, &offset, state->header.record_capacity);
    canonical_texture_encode_word(
        encoded, &offset, state->header.record_word_count);
    canonical_texture_encode_word(
        encoded, &offset, state->header.resource_id_scheme);
    for (index = 0; index < 5; index++) {
        canonical_texture_encode_word(
            encoded, &offset, state->header.reserved[index]);
    }

    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY;
         index++) {
        const AcgcGxCanonicalTextureRecord* record = &state->records[index];

        canonical_texture_encode_word(encoded, &offset, record->flags);
        canonical_texture_encode_word(
            encoded, &offset, record->image_resource_id);
        canonical_texture_encode_word(
            encoded, &offset, record->image_owner_epoch);
        canonical_texture_encode_word(
            encoded, &offset, record->image_generation_lo);
        canonical_texture_encode_word(
            encoded, &offset, record->image_generation_hi);
        canonical_texture_encode_word(encoded, &offset, record->width);
        canonical_texture_encode_word(encoded, &offset, record->height);
        canonical_texture_encode_word(
            encoded, &offset, record->image_format);
        canonical_texture_encode_word(encoded, &offset, record->wrap_s);
        canonical_texture_encode_word(encoded, &offset, record->wrap_t);
        canonical_texture_encode_word(encoded, &offset, record->min_filter);
        canonical_texture_encode_word(encoded, &offset, record->mag_filter);
        canonical_texture_encode_word(encoded, &offset, record->min_lod_q4);
        canonical_texture_encode_word(encoded, &offset, record->max_lod_q4);
        canonical_texture_encode_word(encoded, &offset, record->lod_bias_q5);
        canonical_texture_encode_word(encoded, &offset, record->bias_clamp);
        canonical_texture_encode_word(encoded, &offset, record->edge_lod);
        canonical_texture_encode_word(
            encoded, &offset, record->max_anisotropy);
        canonical_texture_encode_word(
            encoded, &offset, record->mip_level_count);
        canonical_texture_encode_word(
            encoded, &offset, record->image_byte_size);
        canonical_texture_encode_word(
            encoded, &offset, record->image_byte_order);
        canonical_texture_encode_word(
            encoded, &offset, record->image_source_kind);
        canonical_texture_encode_word(
            encoded, &offset, record->tlut_resource_id);
        canonical_texture_encode_word(
            encoded, &offset, record->tlut_owner_epoch);
        canonical_texture_encode_word(
            encoded, &offset, record->tlut_generation_lo);
        canonical_texture_encode_word(
            encoded, &offset, record->tlut_generation_hi);
        canonical_texture_encode_word(encoded, &offset, record->tlut_name);
        canonical_texture_encode_word(encoded, &offset, record->tlut_format);
        canonical_texture_encode_word(
            encoded, &offset, record->tlut_entry_count);
        canonical_texture_encode_word(
            encoded, &offset, record->tlut_byte_size);
        canonical_texture_encode_word(
            encoded, &offset, record->tlut_byte_order);
        canonical_texture_encode_word(
            encoded, &offset, record->tlut_source_kind);
        for (uint32_t reserved_index = 0; reserved_index < 4;
             reserved_index++) {
            canonical_texture_encode_word(
                encoded, &offset, record->reserved[reserved_index]);
        }
    }

    if (offset != ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE) {
        return 0;
    }
    for (index = 0; index < ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE; index++) {
        destination[index] = encoded[index];
    }
    return 1;
}
