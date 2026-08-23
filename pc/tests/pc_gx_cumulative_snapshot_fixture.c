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
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static uint8_t s_section_storage[PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES];
static uint8_t s_destination[PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES];

static const size_t s_fixed_section_sizes[
    PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT] = {
        0,
        ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE,
        ACGC_GX_CANONICAL_CHANNEL_STATE_SIZE,
        ACGC_GX_CANONICAL_TEXGEN_STATE_SIZE,
        ACGC_GX_CANONICAL_TEXTURE_STATE_SIZE,
        ACGC_GX_CANONICAL_TEV_STATE_SIZE,
        ACGC_GX_CANONICAL_LIGHTING_STATE_SIZE,
        ACGC_GX_CANONICAL_BLEND_STATE_SIZE,
        ACGC_GX_CANONICAL_ALPHA_STATE_SIZE,
        ACGC_GX_CANONICAL_DEPTH_STATE_SIZE,
        ACGC_GX_CANONICAL_RASTER_STATE_SIZE,
        ACGC_GX_CANONICAL_FOG_STATE_SIZE,
        ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE,
        ACGC_GX_CANONICAL_DYNAMIC_STATE_SIZE
    };

static const uint32_t s_section_masks[
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

static const uint32_t s_section_counts[
    PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT] = {
        ACGC_GX_CANONICAL_GEOMETRY_STATE_COUNT,
        ACGC_GX_CANONICAL_TRANSFORM_STATE_COUNT,
        ACGC_GX_CANONICAL_CHANNEL_STATE_COUNT,
        ACGC_GX_CANONICAL_TEXGEN_STATE_COUNT,
        ACGC_GX_CANONICAL_TEXTURE_STATE_COUNT,
        3,
        ACGC_GX_CANONICAL_LIGHTING_STATE_COUNT,
        ACGC_GX_CANONICAL_BLEND_STATE_COUNT,
        ACGC_GX_CANONICAL_ALPHA_STATE_COUNT,
        ACGC_GX_CANONICAL_DEPTH_STATE_COUNT,
        ACGC_GX_CANONICAL_RASTER_STATE_COUNT,
        1,
        ACGC_GX_CANONICAL_INDIRECT_SECTION_COUNT,
        ACGC_GX_CANONICAL_DYNAMIC_STATE_COUNT
    };

static const uint32_t s_section_capacities[
    PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT] = {
        ACGC_GX_CANONICAL_GEOMETRY_STATE_CAPACITY,
        ACGC_GX_CANONICAL_TRANSFORM_STATE_CAPACITY,
        ACGC_GX_CANONICAL_CHANNEL_STATE_CAPACITY,
        ACGC_GX_CANONICAL_TEXGEN_STATE_CAPACITY,
        ACGC_GX_CANONICAL_TEXTURE_STATE_CAPACITY,
        ACGC_GX_CANONICAL_TEV_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_LIGHTING_STATE_CAPACITY,
        ACGC_GX_CANONICAL_BLEND_STATE_CAPACITY,
        ACGC_GX_CANONICAL_ALPHA_STATE_CAPACITY,
        ACGC_GX_CANONICAL_DEPTH_STATE_CAPACITY,
        ACGC_GX_CANONICAL_RASTER_STATE_CAPACITY,
        1,
        ACGC_GX_CANONICAL_INDIRECT_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_DYNAMIC_STATE_CAPACITY
    };

static size_t section_size_for_geometry(size_t geometry_size) {
    size_t total = geometry_size;
    uint32_t index;

    for (index = 1;
         index < PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT;
         index++) {
        total += s_fixed_section_sizes[index];
    }
    return total;
}

static void initialize_sections(
    PCGXCumulativeSnapshotSection sections[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT],
    size_t geometry_size
) {
    size_t storage_offset = 0;
    uint32_t index;

    memset(sections, 0, sizeof(*sections) *
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT);
    memset(s_section_storage, 0, sizeof(s_section_storage));
    for (index = 0;
         index < PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT;
         index++) {
        const size_t section_size = index == 0 ?
            geometry_size : s_fixed_section_sizes[index];
        const uint8_t pattern = (uint8_t)(0x10u + index);

        sections[index].section_id = index + 1;
        sections[index].section_version =
            ACGC_GX_CANONICAL_SECTION_VERSION;
        sections[index].byte_size = section_size;
        sections[index].count = s_section_counts[index];
        sections[index].capacity = s_section_capacities[index];
        sections[index].valid_mask = s_section_masks[index];
        sections[index].bytes.data = s_section_storage + storage_offset;
        sections[index].bytes.size = section_size;
        memset(s_section_storage + storage_offset, pattern, section_size);
        storage_offset += section_size;
    }
}

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static int assemble_success(
    PCGXCumulativeSnapshotSection sections[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT],
    size_t destination_capacity,
    size_t expected_size
) {
    size_t actual_size = 0;
    uint32_t index;
    size_t offset;

    memset(s_destination, 0xCD, sizeof(s_destination));
    CHECK(pc_gx_cumulative_snapshot_assemble(
        sections, s_destination, destination_capacity, &actual_size));
    CHECK(actual_size == expected_size);
    CHECK(read_le32(s_destination + 0) == ACGC_GX_CANONICAL_ENVELOPE_MAGIC);
    CHECK(read_le32(s_destination + 4) == ACGC_GX_CANONICAL_ENVELOPE_VERSION);
    CHECK(read_le32(s_destination + 8) ==
        ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE);
    CHECK(read_le32(s_destination + 12) ==
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE);
    CHECK(read_le32(s_destination + 16) ==
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT);
    CHECK(read_le32(s_destination + 20) ==
        PC_GX_CUMULATIVE_SNAPSHOT_FULL_MASK);
    CHECK(read_le32(s_destination + 24) ==
        PC_GX_CUMULATIVE_SNAPSHOT_FULL_MASK);
    CHECK(read_le32(s_destination + 28) ==
        PC_GX_CUMULATIVE_SNAPSHOT_FULL_MASK);
    CHECK(read_le32(s_destination + 32) ==
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET);
    CHECK(read_le32(s_destination + 36) == expected_size -
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET);
    CHECK(read_le32(s_destination + 40) == expected_size);
    CHECK(read_le32(s_destination + 44) == 0);

    offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    for (index = 0;
         index < PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT;
         index++) {
        const size_t directory_offset =
            ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET +
            index * ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE;
        const uint8_t pattern = (uint8_t)(0x10u + index);
        size_t byte_index;

        CHECK(read_le32(s_destination + directory_offset) == index + 1);
        CHECK(read_le32(s_destination + directory_offset + 4) == 1);
        CHECK(read_le32(s_destination + directory_offset + 8) == offset);
        CHECK(read_le32(s_destination + directory_offset + 12) ==
            sections[index].byte_size);
        CHECK(read_le32(s_destination + directory_offset + 16) ==
            sections[index].count);
        CHECK(read_le32(s_destination + directory_offset + 20) ==
            sections[index].capacity);
        CHECK(read_le32(s_destination + directory_offset + 24) ==
            sections[index].valid_mask);
        CHECK(read_le32(s_destination + directory_offset + 28) == 0);
        for (byte_index = 0;
             byte_index < sections[index].byte_size;
             byte_index++) {
            CHECK(s_destination[offset + byte_index] == pattern);
        }
        offset += sections[index].byte_size;
    }
    CHECK(offset == expected_size);
    return 0;
}

static int test_golden_bytes(void) {
    PCGXCumulativeSnapshotSection sections[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT];

    initialize_sections(
        sections, ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE);
    return assemble_success(
        sections,
        PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES,
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
            section_size_for_geometry(
                ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE));
}

static int test_maximum_geometry_size(void) {
    PCGXCumulativeSnapshotSection sections[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT];

    initialize_sections(
        sections, ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE);
    return assemble_success(
        sections,
        PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES,
        PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES);
}

static int test_failure_immutability(
    PCGXCumulativeSnapshotSection sections[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT],
    size_t destination_capacity
) {
    uint8_t before[PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES];
    size_t before_byte_size = SIZE_MAX;

    memset(s_destination, 0xA5, sizeof(s_destination));
    memcpy(before, s_destination, sizeof(before));
    CHECK(!pc_gx_cumulative_snapshot_assemble(
        sections, s_destination, destination_capacity, &before_byte_size));
    CHECK(memcmp(s_destination, before, sizeof(before)) == 0);
    CHECK(before_byte_size == SIZE_MAX);
    return 0;
}

static int test_malformed_metadata(void) {
    PCGXCumulativeSnapshotSection sections[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT];
    uint32_t original_id;
    uint32_t original_version;
    size_t original_size;
    size_t original_span_size;
    uint32_t original_mask;

    initialize_sections(
        sections, ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE);
    original_id = sections[1].section_id;
    sections[1].section_id = sections[0].section_id;
    CHECK(test_failure_immutability(
        sections, PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES) == 0);
    sections[1].section_id = original_id;

    original_version = sections[4].section_version;
    sections[4].section_version++;
    CHECK(test_failure_immutability(
        sections, PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES) == 0);
    sections[4].section_version = original_version;

    original_size = sections[7].byte_size;
    original_span_size = sections[7].bytes.size;
    sections[7].byte_size = original_size - 4;
    sections[7].bytes.size = original_span_size - 4;
    CHECK(test_failure_immutability(
        sections, PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES) == 0);
    sections[7].byte_size = original_size;
    sections[7].bytes.size = original_span_size;

    original_mask = sections[8].valid_mask;
    sections[8].valid_mask = 0;
    CHECK(test_failure_immutability(
        sections, PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES) == 0);
    sections[8].valid_mask = original_mask;
    return 0;
}

static int test_capacity_overflow_and_alias_rejection(void) {
    PCGXCumulativeSnapshotSection sections[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT];
    const size_t expected_size = PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES;
    uint8_t before_storage[PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES];
    const uint8_t* original_data;
    size_t original_size;
    size_t unchanged_size = SIZE_MAX;

    initialize_sections(
        sections, ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE);
    CHECK(test_failure_immutability(sections, expected_size - 1) == 0);

    original_size = sections[3].byte_size;
    sections[3].byte_size = SIZE_MAX;
    sections[3].bytes.size = SIZE_MAX;
    CHECK(test_failure_immutability(
        sections, PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES) == 0);
    sections[3].byte_size = original_size;
    sections[3].bytes.size = original_size;

    memcpy(before_storage, s_section_storage, sizeof(before_storage));
    CHECK(!pc_gx_cumulative_snapshot_assemble(
        sections,
        (uint8_t*)sections[0].bytes.data,
        PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES,
        &unchanged_size));
    CHECK(memcmp(s_section_storage, before_storage,
        sizeof(before_storage)) == 0);
    CHECK(unchanged_size == SIZE_MAX);

    original_data = sections[1].bytes.data;
    sections[1].bytes.data = sections[0].bytes.data;
    CHECK(test_failure_immutability(
        sections, PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES) == 0);
    sections[1].bytes.data = original_data;
    return 0;
}

int main(void) {
    if (test_golden_bytes() != 0 ||
        test_maximum_geometry_size() != 0 ||
        test_malformed_metadata() != 0 ||
        test_capacity_overflow_and_alias_rejection() != 0) {
        return 1;
    }

    puts("pc cumulative canonical envelope assembler fixture: PASS");
    puts("proof boundary: deterministic assembly of prebuilt valid section byte spans only; no producer, resource, callback, flush, renderer, Metal, device, or playability claim");
    return 0;
}
