#include "acgc/gx_canonical_dynamic_state.h"

static uint32_t canonical_dynamic_popcount(uint32_t value) {
    uint32_t count = 0;

    while (value != 0) {
        count += value & UINT32_C(1);
        value >>= 1;
    }
    return count;
}

static int canonical_dynamic_image_format_is_valid(uint32_t format) {
    switch (format) {
        case ACGC_GX_CANONICAL_DYNAMIC_FORMAT_I4:
        case ACGC_GX_CANONICAL_DYNAMIC_FORMAT_I8:
        case ACGC_GX_CANONICAL_DYNAMIC_FORMAT_IA4:
        case ACGC_GX_CANONICAL_DYNAMIC_FORMAT_IA8:
        case ACGC_GX_CANONICAL_DYNAMIC_FORMAT_RGB565:
        case ACGC_GX_CANONICAL_DYNAMIC_FORMAT_RGB5A3:
        case ACGC_GX_CANONICAL_DYNAMIC_FORMAT_RGBA8:
        case ACGC_GX_CANONICAL_DYNAMIC_FORMAT_C4:
        case ACGC_GX_CANONICAL_DYNAMIC_FORMAT_C8:
        case ACGC_GX_CANONICAL_DYNAMIC_FORMAT_C14X2:
        case ACGC_GX_CANONICAL_DYNAMIC_FORMAT_CMPR:
            return 1;
        default:
            return 0;
    }
}

static int canonical_dynamic_tlut_format_is_valid(uint32_t format) {
    return format == ACGC_GX_CANONICAL_DYNAMIC_TLUT_FORMAT_IA8 ||
        format == ACGC_GX_CANONICAL_DYNAMIC_TLUT_FORMAT_RGB565 ||
        format == ACGC_GX_CANONICAL_DYNAMIC_TLUT_FORMAT_RGB5A3;
}

static int canonical_dynamic_source_kind_is_valid(uint32_t source_kind) {
    return source_kind ==
            ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_RAW_GUEST ||
        source_kind ==
            ACGC_GX_CANONICAL_DYNAMIC_SOURCE_KIND_EMU64_CONVERTED;
}

static int canonical_dynamic_byte_order_is_valid(uint32_t byte_order) {
    return byte_order == ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_GX_BE ||
        byte_order == ACGC_GX_CANONICAL_DYNAMIC_BYTE_ORDER_LE;
}

static int canonical_dynamic_reserved_words_are_zero(
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

static int canonical_dynamic_byte_flags_are_valid(uint32_t byte_flags) {
    const uint32_t ownership_flags =
        byte_flags & (ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED |
                      ACGC_GX_CANONICAL_DYNAMIC_BYTES_OWNED);

    if ((byte_flags & ~ACGC_GX_CANONICAL_DYNAMIC_BYTES_FLAG_MASK) != 0) {
        return 0;
    }
    if ((byte_flags & ACGC_GX_CANONICAL_DYNAMIC_BYTES_AVAILABLE) != 0) {
        return ownership_flags == ACGC_GX_CANONICAL_DYNAMIC_BYTES_BORROWED ||
            ownership_flags == ACGC_GX_CANONICAL_DYNAMIC_BYTES_OWNED;
    }
    return ownership_flags == 0;
}

static int canonical_dynamic_record_is_zero(
    const AcgcGxCanonicalDynamicRecord* record
) {
    return record != NULL &&
        record->resource_id == 0 &&
        record->kind == 0 &&
        record->owner_epoch == 0 &&
        record->generation_lo == 0 &&
        record->generation_hi == 0 &&
        record->owner_slot == 0 &&
        record->byte_flags == 0 &&
        record->byte_size == 0 &&
        record->byte_order == 0 &&
        record->alignment == 0 &&
        record->source_kind == 0 &&
        record->format == 0 &&
        record->element_count == 0 &&
        canonical_dynamic_reserved_words_are_zero(record->reserved, 3);
}

int acgc_gx_canonical_dynamic_state_validate(
    const AcgcGxCanonicalDynamicState* state
) {
    const AcgcGxCanonicalDynamicHeader* header;
    uint32_t expected_present_count;
    uint32_t record_index;

    if (state == NULL) {
        return 0;
    }

    header = &state->header;
    if (header->owner_epoch == 0 ||
        (header->present_image_mask & ~UINT32_C(0x000000FF)) != 0 ||
        (header->present_tlut_mask & ~UINT32_C(0x0000FFFF)) != 0 ||
        (header->required_image_mask & ~header->present_image_mask) != 0 ||
        (header->required_tlut_mask & ~header->present_tlut_mask) != 0 ||
        header->record_byte_offset !=
            ACGC_GX_CANONICAL_DYNAMIC_HEADER_BYTE_SIZE ||
        header->record_count != ACGC_GX_CANONICAL_DYNAMIC_STATE_COUNT ||
        header->record_capacity != ACGC_GX_CANONICAL_DYNAMIC_STATE_CAPACITY ||
        header->record_word_count !=
            ACGC_GX_CANONICAL_DYNAMIC_RECORD_WORD_COUNT ||
        header->resource_id_scheme !=
            ACGC_GX_CANONICAL_DYNAMIC_RESOURCE_ID_SCHEME ||
        !canonical_dynamic_reserved_words_are_zero(header->reserved, 5)) {
        return 0;
    }

    expected_present_count =
        canonical_dynamic_popcount(header->present_image_mask) +
        canonical_dynamic_popcount(header->present_tlut_mask);
    if (header->present_resource_count != expected_present_count) {
        return 0;
    }

    for (record_index = 0;
         record_index < ACGC_GX_CANONICAL_DYNAMIC_STATE_CAPACITY;
         record_index++) {
        const AcgcGxCanonicalDynamicRecord* record =
            &state->records[record_index];
        const int is_image = record_index <
            ACGC_GX_CANONICAL_DYNAMIC_IMAGE_MAP_COUNT;
        const uint32_t slot = is_image ? record_index :
            record_index - ACGC_GX_CANONICAL_DYNAMIC_IMAGE_MAP_COUNT;
        const uint32_t slot_mask = UINT32_C(1) << slot;
        const int present = is_image ?
            (header->present_image_mask & slot_mask) != 0 :
            (header->present_tlut_mask & slot_mask) != 0;

        if (!present) {
            if (!canonical_dynamic_record_is_zero(record)) {
                return 0;
            }
            continue;
        }

        if (record->resource_id != (is_image ?
                ACGC_GX_CANONICAL_DYNAMIC_IMAGE_RESOURCE_ID_BASE + slot :
                ACGC_GX_CANONICAL_DYNAMIC_TLUT_RESOURCE_ID_BASE + slot) ||
            record->kind != (is_image ?
                ACGC_GX_CANONICAL_DYNAMIC_KIND_IMAGE :
                ACGC_GX_CANONICAL_DYNAMIC_KIND_TLUT) ||
            record->owner_epoch != header->owner_epoch ||
            (record->generation_lo == 0 && record->generation_hi == 0) ||
            record->owner_slot != slot ||
            !canonical_dynamic_byte_flags_are_valid(record->byte_flags) ||
            record->byte_size == 0 || record->byte_size == UINT32_MAX ||
            !canonical_dynamic_byte_order_is_valid(record->byte_order) ||
            record->alignment != ACGC_GX_CANONICAL_DYNAMIC_ALIGNMENT_BYTES ||
            !canonical_dynamic_source_kind_is_valid(record->source_kind) ||
            !canonical_dynamic_reserved_words_are_zero(record->reserved, 3)) {
            return 0;
        }

        if (is_image) {
            if (!canonical_dynamic_image_format_is_valid(record->format) ||
                record->element_count != 0) {
                return 0;
            }
        } else if (!canonical_dynamic_tlut_format_is_valid(record->format) ||
                   record->element_count == 0 ||
                   record->element_count >
                       ACGC_GX_CANONICAL_DYNAMIC_TLUT_ENTRY_MAX ||
                   (uint64_t)record->element_count * UINT64_C(2) !=
                       (uint64_t)record->byte_size) {
            return 0;
        }
    }

    return 1;
}

static int canonical_dynamic_entry_is_absent(
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry
) {
    return entry != NULL &&
        entry->section_id == ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID &&
        entry->section_version == 0 &&
        entry->byte_offset == 0 &&
        entry->byte_size == 0 &&
        entry->count == 0 &&
        entry->capacity == 0 &&
        entry->valid_mask == 0 &&
        entry->reserved == 0;
}

int acgc_gx_canonical_dynamic_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
) {
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    if (!acgc_gx_canonical_envelope_validate(
            envelope, envelope_byte_size)) {
        return 0;
    }

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID - 1];
    if ((envelope->header.present_state_mask &
            ACGC_GX_CANONICAL_DYNAMIC_SECTION_MASK) == 0) {
        return canonical_dynamic_entry_is_absent(entry);
    }

    return entry->section_id == ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID &&
        entry->section_version == ACGC_GX_CANONICAL_DYNAMIC_SECTION_VERSION &&
        entry->byte_size == ACGC_GX_CANONICAL_DYNAMIC_SECTION_BYTE_SIZE &&
        entry->count == ACGC_GX_CANONICAL_DYNAMIC_SECTION_COUNT &&
        entry->capacity == ACGC_GX_CANONICAL_DYNAMIC_SECTION_CAPACITY &&
        entry->valid_mask == ACGC_GX_CANONICAL_DYNAMIC_SECTION_MASK &&
        entry->reserved == 0;
}
