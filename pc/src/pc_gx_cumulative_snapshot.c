#include "pc_gx_cumulative_snapshot.h"

#include "acgc/gx_canonical_alpha_state.h"
#include "acgc/gx_canonical_blend_state.h"
#include "acgc/gx_canonical_channel_state.h"
#include "acgc/gx_canonical_depth_state.h"
#include "acgc/gx_canonical_dynamic_state.h"
#include "acgc/gx_canonical_geometry_state.h"
#include "acgc/gx_canonical_indirect_state.h"
#include "acgc/gx_canonical_lighting_state.h"
#include "acgc/gx_canonical_raster_state.h"
#include "acgc/gx_canonical_tev_state.h"
#include "acgc/gx_canonical_texgen_state.h"
#include "acgc/gx_canonical_texture_state.h"
#include "acgc/gx_canonical_transform_state.h"

#include <stdint.h>
#include <string.h>

typedef int (*PCGXCumulativeSnapshotMetadataValidator)(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
);

static const PCGXCumulativeSnapshotMetadataValidator
    s_metadata_validators[PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT] = {
        acgc_gx_canonical_geometry_metadata_validate,
        acgc_gx_canonical_transform_metadata_validate,
        acgc_gx_canonical_channel_metadata_validate,
        acgc_gx_canonical_texgen_metadata_validate,
        acgc_gx_canonical_texture_metadata_validate,
        acgc_gx_canonical_tev_metadata_validate,
        acgc_gx_canonical_lighting_metadata_validate,
        acgc_gx_canonical_blend_metadata_validate,
        acgc_gx_canonical_alpha_metadata_validate,
        acgc_gx_canonical_depth_metadata_validate,
        acgc_gx_canonical_raster_metadata_validate,
        NULL,
        acgc_gx_canonical_indirect_metadata_validate,
        acgc_gx_canonical_dynamic_metadata_validate
    };

static const uint32_t s_expected_section_ids[
    PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT] = {
        ACGC_GX_CANONICAL_SECTION_ID_GEOMETRY,
        ACGC_GX_CANONICAL_SECTION_ID_TRANSFORMS,
        ACGC_GX_CANONICAL_SECTION_ID_CHANNELS,
        ACGC_GX_CANONICAL_SECTION_ID_TEXGENS,
        ACGC_GX_CANONICAL_SECTION_ID_TEXTURES,
        ACGC_GX_CANONICAL_SECTION_ID_TEV,
        ACGC_GX_CANONICAL_SECTION_ID_LIGHTING,
        ACGC_GX_CANONICAL_SECTION_ID_BLEND,
        ACGC_GX_CANONICAL_SECTION_ID_ALPHA,
        ACGC_GX_CANONICAL_SECTION_ID_DEPTH,
        ACGC_GX_CANONICAL_SECTION_ID_RASTER,
        ACGC_GX_CANONICAL_SECTION_ID_FOG,
        ACGC_GX_CANONICAL_SECTION_ID_INDIRECT,
        ACGC_GX_CANONICAL_SECTION_ID_DYNAMIC
    };

static const uint32_t s_expected_section_masks[
    PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT] = {
        ACGC_GX_CANONICAL_SECTION_MASK_GEOMETRY,
        ACGC_GX_CANONICAL_SECTION_MASK_TRANSFORMS,
        ACGC_GX_CANONICAL_SECTION_MASK_CHANNELS,
        ACGC_GX_CANONICAL_SECTION_MASK_TEXGENS,
        ACGC_GX_CANONICAL_SECTION_MASK_TEXTURES,
        ACGC_GX_CANONICAL_SECTION_MASK_TEV,
        ACGC_GX_CANONICAL_SECTION_MASK_LIGHTING,
        ACGC_GX_CANONICAL_SECTION_MASK_BLEND,
        ACGC_GX_CANONICAL_SECTION_MASK_ALPHA,
        ACGC_GX_CANONICAL_SECTION_MASK_DEPTH,
        ACGC_GX_CANONICAL_SECTION_MASK_RASTER,
        ACGC_GX_CANONICAL_SECTION_MASK_FOG,
        ACGC_GX_CANONICAL_SECTION_MASK_INDIRECT,
        ACGC_GX_CANONICAL_SECTION_MASK_DYNAMIC
    };

static void cumulative_snapshot_write_le32(
    uint8_t* destination,
    uint32_t value
) {
    destination[0] = (uint8_t)(value & UINT32_C(0xFF));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    destination[3] = (uint8_t)((value >> 24) & UINT32_C(0xFF));
}

static int cumulative_snapshot_pointer_range(
    const void* data,
    size_t size,
    uintptr_t* begin,
    uintptr_t* end
) {
    uintptr_t address;
    uintptr_t width;

    if (data == NULL || size == 0 ||
        (uintmax_t)size > (uintmax_t)UINTPTR_MAX) {
        return 0;
    }

    address = (uintptr_t)data;
    width = (uintptr_t)size;
    if (width > UINTPTR_MAX - address) {
        return 0;
    }

    if (begin != NULL) {
        *begin = address;
    }
    if (end != NULL) {
        *end = address + width;
    }
    return 1;
}

static int cumulative_snapshot_ranges_overlap(
    uintptr_t first_begin,
    uintptr_t first_end,
    uintptr_t second_begin,
    uintptr_t second_end
) {
    return first_begin < second_end && second_begin < first_end;
}

static int cumulative_snapshot_validate_sections(
    const PCGXCumulativeSnapshotSection* sections,
    size_t* payload_byte_size
) {
    size_t payload_size = 0;
    uint32_t index;

    if (sections == NULL || payload_byte_size == NULL) {
        return 0;
    }

    for (index = 0;
         index < PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT;
         index++) {
        const PCGXCumulativeSnapshotSection* section = &sections[index];
        const size_t section_size = section->byte_size;

        if (section->section_id != s_expected_section_ids[index] ||
            section->section_version != ACGC_GX_CANONICAL_SECTION_VERSION ||
            section->valid_mask != s_expected_section_masks[index] ||
            section_size != section->bytes.size ||
            section_size == 0 ||
            section_size > (size_t)UINT32_MAX ||
            (section_size % ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT) != 0 ||
            !cumulative_snapshot_pointer_range(
                section->bytes.data, section_size, NULL, NULL)) {
            return 0;
        }

        /* Keep the sum bounded before any offset or total-size conversion. */
        if (payload_size >
                (size_t)PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES -
                    ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET ||
            section_size >
                (size_t)PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES -
                    ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET -
                    payload_size) {
            return 0;
        }
        payload_size += section_size;
    }

    *payload_byte_size = payload_size;
    return 1;
}

static int cumulative_snapshot_validate_aliases(
    const PCGXCumulativeSnapshotSection* sections,
    uint8_t* destination,
    size_t destination_capacity
) {
    uintptr_t metadata_begin;
    uintptr_t metadata_end;
    uintptr_t destination_begin;
    uintptr_t destination_end;
    uintptr_t section_begin;
    uintptr_t section_end;
    uint32_t index;
    uint32_t other;

    if (!cumulative_snapshot_pointer_range(
            sections,
            sizeof(*sections) * PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT,
            &metadata_begin,
            &metadata_end) ||
        !cumulative_snapshot_pointer_range(
            destination,
            destination_capacity,
            &destination_begin,
            &destination_end)) {
        return 0;
    }

    if (cumulative_snapshot_ranges_overlap(
            metadata_begin, metadata_end,
            destination_begin, destination_end)) {
        return 0;
    }

    for (index = 0;
         index < PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT;
         index++) {
        const PCGXCumulativeSnapshotSection* section = &sections[index];

        if (!cumulative_snapshot_pointer_range(
                section->bytes.data,
                section->bytes.size,
                &section_begin,
                &section_end) ||
            cumulative_snapshot_ranges_overlap(
                section_begin, section_end,
                metadata_begin, metadata_end) ||
            cumulative_snapshot_ranges_overlap(
                section_begin, section_end,
                destination_begin, destination_end)) {
            return 0;
        }

        for (other = 0; other < index; other++) {
            uintptr_t other_begin;
            uintptr_t other_end;

            if (!cumulative_snapshot_pointer_range(
                    sections[other].bytes.data,
                    sections[other].bytes.size,
                    &other_begin,
                    &other_end) ||
                cumulative_snapshot_ranges_overlap(
                    section_begin, section_end,
                    other_begin, other_end)) {
                return 0;
            }
        }
    }
    return 1;
}

static int cumulative_snapshot_prepare_metadata(
    const PCGXCumulativeSnapshotSection* sections,
    size_t payload_byte_size,
    size_t total_byte_size,
    AcgcGxCanonicalEnvelope* envelope
) {
    size_t offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    uint32_t index;

    if (sections == NULL || envelope == NULL ||
        payload_byte_size > (size_t)UINT32_MAX ||
        total_byte_size > (size_t)UINT32_MAX ||
        !acgc_gx_canonical_envelope_init(envelope)) {
        return 0;
    }

    envelope->header.present_state_mask =
        PC_GX_CUMULATIVE_SNAPSHOT_FULL_MASK;
    envelope->header.required_state_mask =
        PC_GX_CUMULATIVE_SNAPSHOT_FULL_MASK;
    envelope->header.payload_byte_size = (uint32_t)payload_byte_size;
    envelope->header.total_byte_size = (uint32_t)total_byte_size;

    for (index = 0;
         index < PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT;
         index++) {
        AcgcGxCanonicalEnvelopeDirectoryEntry* entry =
            &envelope->directory[index];

        entry->section_id = sections[index].section_id;
        entry->section_version = sections[index].section_version;
        entry->byte_offset = (uint32_t)offset;
        entry->byte_size = (uint32_t)sections[index].byte_size;
        entry->count = sections[index].count;
        entry->capacity = sections[index].capacity;
        entry->valid_mask = sections[index].valid_mask;
        entry->reserved = 0;
        offset += sections[index].byte_size;
    }

    return offset == total_byte_size;
}

static int cumulative_snapshot_validate_metadata(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t total_byte_size
) {
    uint32_t index;

    if (!acgc_gx_canonical_envelope_validate(envelope, total_byte_size)) {
        return 0;
    }

    for (index = 0;
         index < PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT;
         index++) {
        const PCGXCumulativeSnapshotMetadataValidator validator =
            s_metadata_validators[index];

        /* The common envelope validator owns Fog's fixed metadata rule. */
        if (validator != NULL && !validator(envelope, total_byte_size)) {
            return 0;
        }
    }
    return 1;
}

static void cumulative_snapshot_write_header(
    uint8_t* destination,
    size_t payload_byte_size,
    size_t total_byte_size
) {
    const uint32_t header_words[] = {
        ACGC_GX_CANONICAL_ENVELOPE_MAGIC,
        ACGC_GX_CANONICAL_ENVELOPE_VERSION,
        ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE,
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE,
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT,
        PC_GX_CUMULATIVE_SNAPSHOT_FULL_MASK,
        PC_GX_CUMULATIVE_SNAPSHOT_FULL_MASK,
        PC_GX_CUMULATIVE_SNAPSHOT_FULL_MASK,
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET,
        (uint32_t)payload_byte_size,
        (uint32_t)total_byte_size,
        0
    };
    uint32_t index;

    for (index = 0;
         index < sizeof(header_words) / sizeof(header_words[0]);
         index++) {
        cumulative_snapshot_write_le32(
            destination + index * sizeof(uint32_t), header_words[index]);
    }
}

int pc_gx_cumulative_snapshot_assemble(
    const PCGXCumulativeSnapshotSection sections[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT],
    uint8_t* destination,
    size_t destination_capacity,
    size_t* destination_byte_size
) {
    AcgcGxCanonicalEnvelope metadata;
    uint8_t assembled[PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES];
    size_t payload_byte_size;
    size_t total_byte_size;
    size_t offset;
    uint32_t index;

    if (sections == NULL || destination == NULL ||
        destination_byte_size == NULL ||
        !cumulative_snapshot_validate_sections(
            sections, &payload_byte_size)) {
        return 0;
    }

    if (payload_byte_size >
            (size_t)PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES -
                ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET) {
        return 0;
    }
    total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + payload_byte_size;
    if (total_byte_size > PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES ||
        destination_capacity < total_byte_size ||
        !cumulative_snapshot_validate_aliases(
            sections, destination, destination_capacity) ||
        !cumulative_snapshot_prepare_metadata(
            sections,
            payload_byte_size,
            total_byte_size,
            &metadata) ||
        !cumulative_snapshot_validate_metadata(&metadata, total_byte_size)) {
        return 0;
    }

    memset(assembled, 0, total_byte_size);
    cumulative_snapshot_write_header(
        assembled, payload_byte_size, total_byte_size);
    offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    for (index = 0;
         index < PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT;
         index++) {
        const PCGXCumulativeSnapshotSection* section = &sections[index];
        const size_t directory_offset =
            ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET +
            index * ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE;
        const uint32_t directory_words[] = {
            section->section_id,
            section->section_version,
            (uint32_t)offset,
            (uint32_t)section->byte_size,
            section->count,
            section->capacity,
            section->valid_mask,
            0
        };
        uint32_t word;

        for (word = 0;
             word < sizeof(directory_words) / sizeof(directory_words[0]);
             word++) {
            cumulative_snapshot_write_le32(
                assembled + directory_offset + word * sizeof(uint32_t),
                directory_words[word]);
        }
        memcpy(assembled + offset, section->bytes.data, section->byte_size);
        offset += section->byte_size;
    }

    if (offset != total_byte_size) {
        return 0;
    }

    memcpy(destination, assembled, total_byte_size);
    *destination_byte_size = total_byte_size;
    return 1;
}
