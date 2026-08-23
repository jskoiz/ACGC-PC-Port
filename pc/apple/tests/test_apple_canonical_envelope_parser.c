#include "acgc/apple_canonical_envelope_parser.h"

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

#include <stdio.h>
#include <string.h>

#define TEST_FULL_PAYLOAD_SIZE \
    (ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE + \
     ACGC_GX_CANONICAL_TRANSFORM_SECTION_BYTE_SIZE + \
     ACGC_GX_CANONICAL_CHANNEL_SECTION_BYTE_SIZE + \
     ACGC_GX_CANONICAL_TEXGEN_SECTION_BYTE_SIZE + \
     ACGC_GX_CANONICAL_TEXTURE_SECTION_BYTE_SIZE + \
     ACGC_GX_CANONICAL_TEV_SECTION_BYTE_SIZE + \
     ACGC_GX_CANONICAL_LIGHTING_SECTION_BYTE_SIZE + \
     ACGC_GX_CANONICAL_BLEND_SECTION_BYTE_SIZE + \
     ACGC_GX_CANONICAL_ALPHA_SECTION_BYTE_SIZE + \
     ACGC_GX_CANONICAL_DEPTH_SECTION_BYTE_SIZE + \
     ACGC_GX_CANONICAL_RASTER_SECTION_BYTE_SIZE + \
     ACGC_GX_CANONICAL_FOG_STATE_SIZE + \
     ACGC_GX_CANONICAL_INDIRECT_SECTION_BYTE_SIZE + \
     ACGC_GX_CANONICAL_DYNAMIC_SECTION_BYTE_SIZE)
#define TEST_FULL_SIZE \
    (ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + TEST_FULL_PAYLOAD_SIZE)

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL:%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static const uint32_t s_fixture_section_ids[
    ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT] = {
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

/* This is an explicit canonical mask mapping, not an index-derived mask. */
static const uint32_t s_fixture_section_masks[
    ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT] = {
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

/* Payload sentinels are transport-only bytes; they are not semantic values. */
static const uint32_t s_fixture_section_sizes[
    ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT] = {
        ACGC_GX_CANONICAL_GEOMETRY_MIN_SECTION_SIZE,
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_BYTE_SIZE,
        ACGC_GX_CANONICAL_CHANNEL_SECTION_BYTE_SIZE,
        ACGC_GX_CANONICAL_TEXGEN_SECTION_BYTE_SIZE,
        ACGC_GX_CANONICAL_TEXTURE_SECTION_BYTE_SIZE,
        ACGC_GX_CANONICAL_TEV_SECTION_BYTE_SIZE,
        ACGC_GX_CANONICAL_LIGHTING_SECTION_BYTE_SIZE,
        ACGC_GX_CANONICAL_BLEND_SECTION_BYTE_SIZE,
        ACGC_GX_CANONICAL_ALPHA_SECTION_BYTE_SIZE,
        ACGC_GX_CANONICAL_DEPTH_SECTION_BYTE_SIZE,
        ACGC_GX_CANONICAL_RASTER_SECTION_BYTE_SIZE,
        ACGC_GX_CANONICAL_FOG_STATE_SIZE,
        ACGC_GX_CANONICAL_INDIRECT_SECTION_BYTE_SIZE,
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_BYTE_SIZE
    };

static const uint32_t s_fixture_section_counts[
    ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT] = {
        ACGC_GX_CANONICAL_GEOMETRY_STATE_COUNT,
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_COUNT,
        ACGC_GX_CANONICAL_CHANNEL_SECTION_COUNT,
        ACGC_GX_CANONICAL_TEXGEN_SECTION_COUNT,
        ACGC_GX_CANONICAL_TEXTURE_SECTION_COUNT,
        ACGC_GX_CANONICAL_TEV_SECTION_COUNT_MAX,
        ACGC_GX_CANONICAL_LIGHTING_SECTION_COUNT,
        ACGC_GX_CANONICAL_BLEND_SECTION_COUNT,
        ACGC_GX_CANONICAL_ALPHA_SECTION_COUNT,
        ACGC_GX_CANONICAL_DEPTH_SECTION_COUNT,
        ACGC_GX_CANONICAL_RASTER_SECTION_COUNT,
        UINT32_C(1),
        ACGC_GX_CANONICAL_INDIRECT_SECTION_COUNT,
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_COUNT
    };

static const uint32_t s_fixture_section_capacities[
    ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT] = {
        ACGC_GX_CANONICAL_GEOMETRY_STATE_CAPACITY,
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_CHANNEL_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_TEXGEN_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_TEXTURE_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_TEV_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_LIGHTING_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_BLEND_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_ALPHA_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_DEPTH_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_RASTER_SECTION_CAPACITY,
        UINT32_C(1),
        ACGC_GX_CANONICAL_INDIRECT_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_CAPACITY
    };

static void put_le32(uint8_t* bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)(value & UINT32_C(0xFF));
    bytes[offset + 1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    bytes[offset + 2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    bytes[offset + 3] = (uint8_t)((value >> 24) & UINT32_C(0xFF));
}

static size_t directory_word_offset(size_t index, size_t word_index)
{
    return (size_t)ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET +
        index * (size_t)ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE +
        word_index * sizeof(uint32_t);
}

static size_t fixture_section_payload_offset(size_t index)
{
    size_t offset = (size_t)ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    for (size_t previous = 0; previous < index; ++previous) {
        offset += s_fixture_section_sizes[previous];
    }
    return offset;
}

static void build_full_fixture(uint8_t* bytes)
{
    memset(bytes, 0, TEST_FULL_SIZE);

    put_le32(bytes, 0, ACGC_GX_CANONICAL_ENVELOPE_MAGIC);
    put_le32(bytes, 4, ACGC_GX_CANONICAL_ENVELOPE_VERSION);
    put_le32(bytes, 8, ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE);
    put_le32(
        bytes,
        12,
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE
    );
    put_le32(
        bytes,
        16,
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT
    );
    put_le32(
        bytes,
        20,
        ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK
    );
    put_le32(
        bytes,
        24,
        ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK
    );
    put_le32(
        bytes,
        28,
        ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK
    );
    put_le32(bytes, 32, ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET);
    put_le32(bytes, 36, TEST_FULL_PAYLOAD_SIZE);
    put_le32(bytes, 40, TEST_FULL_SIZE);
    put_le32(bytes, 44, 0);

    for (size_t index = 0;
         index < ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT;
         ++index) {
        const size_t payload_offset = fixture_section_payload_offset(index);
        put_le32(
            bytes,
            directory_word_offset(index, 0),
            s_fixture_section_ids[index]
        );
        put_le32(
            bytes,
            directory_word_offset(index, 1),
            ACGC_GX_CANONICAL_SECTION_VERSION
        );
        put_le32(bytes, directory_word_offset(index, 2), (uint32_t)payload_offset);
        put_le32(
            bytes,
            directory_word_offset(index, 3),
            s_fixture_section_sizes[index]
        );
        put_le32(
            bytes,
            directory_word_offset(index, 4),
            s_fixture_section_counts[index]
        );
        put_le32(
            bytes,
            directory_word_offset(index, 5),
            s_fixture_section_capacities[index]
        );
        put_le32(
            bytes,
            directory_word_offset(index, 6),
            s_fixture_section_masks[index]
        );
        put_le32(bytes, directory_word_offset(index, 7), 0);

        /* Distinctive transport bytes ensure no native word cast is needed. */
        bytes[payload_offset] = (uint8_t)(0xA0U + index);
        bytes[payload_offset + 1] = (uint8_t)(0x50U + index);
        bytes[payload_offset + 2] = (uint8_t)(0x0FU + index);
        bytes[payload_offset + 3] = (uint8_t)(0xC0U - index);
    }
}

static void build_empty_fixture(uint8_t* bytes)
{
    memset(bytes, 0, (size_t)ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET);
    put_le32(bytes, 0, ACGC_GX_CANONICAL_ENVELOPE_MAGIC);
    put_le32(bytes, 4, ACGC_GX_CANONICAL_ENVELOPE_VERSION);
    put_le32(bytes, 8, ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE);
    put_le32(
        bytes,
        12,
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE
    );
    put_le32(
        bytes,
        16,
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT
    );
    put_le32(
        bytes,
        20,
        ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK
    );
    put_le32(bytes, 24, 0);
    put_le32(bytes, 28, 0);
    put_le32(bytes, 32, ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET);
    put_le32(bytes, 36, 0);
    put_le32(bytes, 40, ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET);
    put_le32(bytes, 44, 0);

    for (size_t index = 0;
         index < ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT;
         ++index) {
        put_le32(
            bytes,
            directory_word_offset(index, 0),
            s_fixture_section_ids[index]
        );
    }
}

static int expect_status_unchanged(
    const uint8_t* bytes,
    size_t byte_size,
    AcgcAppleCanonicalEnvelopeParserStatus expected,
    const char* label
)
{
    AcgcAppleCanonicalEnvelopeView output;
    uint8_t before[sizeof(output)];
    memset(&output, 0xA5, sizeof(output));
    memcpy(before, &output, sizeof(output));

    const AcgcAppleCanonicalEnvelopeParserStatus actual =
        acgc_apple_canonical_envelope_parse(bytes, byte_size, &output);
    if (actual != expected) {
        fprintf(
            stderr,
            "FAIL:%s: expected %s, got %s\n",
            label,
            acgc_apple_canonical_envelope_status_string(expected),
            acgc_apple_canonical_envelope_status_string(actual)
        );
        return 0;
    }
    if (memcmp(before, &output, sizeof(output)) != 0) {
        fprintf(stderr, "FAIL:%s: output changed on error\n", label);
        return 0;
    }
    return 1;
}

static int test_golden_full_envelope(void)
{
    uint8_t bytes[TEST_FULL_SIZE];
    build_full_fixture(bytes);

    CHECK(bytes[0] == 0x58 && bytes[1] == 0x47 &&
        bytes[2] == 0x43 && bytes[3] == 0x41);
    CHECK(bytes[(size_t)ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET] == 0xA0);
    CHECK(bytes[(size_t)ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 1] == 0x50);
    CHECK(bytes[(size_t)ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 2] == 0x0F);
    CHECK(bytes[(size_t)ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 3] == 0xC0);

    AcgcAppleCanonicalEnvelopeView view;
    CHECK(
        acgc_apple_canonical_envelope_parse(bytes, sizeof(bytes), &view) ==
        ACGC_APPLE_CANONICAL_ENVELOPE_OK
    );
    CHECK(view.magic == ACGC_GX_CANONICAL_ENVELOPE_MAGIC);
    CHECK(view.version == ACGC_GX_CANONICAL_ENVELOPE_VERSION);
    CHECK(view.header_byte_size == ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE);
    CHECK(view.directory_entry_byte_size ==
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE);
    CHECK(view.directory_count == ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT);
    CHECK(view.known_state_mask == ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK);
    CHECK(view.present_state_mask == ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK);
    CHECK(view.required_state_mask == ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK);
    CHECK(view.payload_offset == ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET);
    CHECK(view.payload_byte_size == TEST_FULL_PAYLOAD_SIZE);
    CHECK(view.total_byte_size == TEST_FULL_SIZE);
    CHECK(view.reserved == 0);

    for (size_t index = 0;
         index < ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT;
         ++index) {
        const AcgcAppleCanonicalEnvelopeSection* section = &view.sections[index];
        CHECK(section->section_id == s_fixture_section_ids[index]);
        CHECK(section->section_version ==
            ACGC_GX_CANONICAL_SECTION_VERSION);
        CHECK(section->byte_offset ==
            fixture_section_payload_offset(index));
        CHECK(section->byte_size == s_fixture_section_sizes[index]);
        CHECK(section->count == s_fixture_section_counts[index]);
        CHECK(section->capacity == s_fixture_section_capacities[index]);
        CHECK(section->valid_mask == s_fixture_section_masks[index]);
        CHECK(section->reserved == 0);
    }
    return 1;
}

static int test_golden_empty_envelope(void)
{
    uint8_t bytes[ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET];
    build_empty_fixture(bytes);

    AcgcAppleCanonicalEnvelopeView view;
    CHECK(
        acgc_apple_canonical_envelope_parse(bytes, sizeof(bytes), &view) ==
        ACGC_APPLE_CANONICAL_ENVELOPE_OK
    );
    CHECK(view.present_state_mask == 0 && view.required_state_mask == 0);
    CHECK(view.payload_byte_size == 0);
    CHECK(view.total_byte_size == ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET);
    for (size_t index = 0;
         index < ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT;
         ++index) {
        CHECK(view.sections[index].section_id == s_fixture_section_ids[index]);
        CHECK(view.sections[index].section_version == 0);
        CHECK(view.sections[index].byte_offset == 0);
        CHECK(view.sections[index].byte_size == 0);
        CHECK(view.sections[index].count == 0);
        CHECK(view.sections[index].capacity == 0);
        CHECK(view.sections[index].valid_mask == 0);
        CHECK(view.sections[index].reserved == 0);
    }
    return 1;
}

static int test_truncation_at_every_prefix(void)
{
    uint8_t bytes[TEST_FULL_SIZE];
    build_full_fixture(bytes);
    for (size_t size = 0; size < TEST_FULL_SIZE; ++size) {
        CHECK(expect_status_unchanged(
            bytes,
            size,
            ACGC_APPLE_CANONICAL_ENVELOPE_TRUNCATED,
            "truncation prefix"
        ));
    }
    return 1;
}

static int test_invalid_header_values(void)
{
    uint8_t valid[TEST_FULL_SIZE];
    uint8_t mutated[TEST_FULL_SIZE];
    build_full_fixture(valid);

#define EXPECT_HEADER_WORD(offset, value, status) \
    do { \
        memcpy(mutated, valid, sizeof(mutated)); \
        put_le32(mutated, (offset), (value)); \
        CHECK(expect_status_unchanged(mutated, sizeof(mutated), (status), #offset)); \
    } while (0)

    EXPECT_HEADER_WORD(0, 0, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER);
    EXPECT_HEADER_WORD(4, 2, ACGC_APPLE_CANONICAL_ENVELOPE_UNSUPPORTED_VERSION);
    EXPECT_HEADER_WORD(8, 44, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER);
    EXPECT_HEADER_WORD(
        12,
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE - 4,
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER
    );
    EXPECT_HEADER_WORD(16, 13, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER);
    EXPECT_HEADER_WORD(20, UINT32_C(0x00007FFF),
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER);
    EXPECT_HEADER_WORD(24, UINT32_C(0x00007FFF),
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER);
    EXPECT_HEADER_WORD(24, UINT32_C(1),
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER);
    EXPECT_HEADER_WORD(28, UINT32_C(0x00004000),
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER);
    EXPECT_HEADER_WORD(32, ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET - 4,
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER);
    EXPECT_HEADER_WORD(36, TEST_FULL_PAYLOAD_SIZE - 2,
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER);
    EXPECT_HEADER_WORD(40, TEST_FULL_SIZE - 4,
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_SIZE);
    EXPECT_HEADER_WORD(44, 1, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER);

#undef EXPECT_HEADER_WORD
    return 1;
}

static int test_invalid_directory_values(void)
{
    uint8_t valid[TEST_FULL_SIZE];
    uint8_t mutated[TEST_FULL_SIZE];
    const uint32_t transform_offset =
        (uint32_t)fixture_section_payload_offset(1);
    build_full_fixture(valid);

#define EXPECT_DIRECTORY_WORD(index, word, value, status) \
    do { \
        memcpy(mutated, valid, sizeof(mutated)); \
        put_le32(mutated, directory_word_offset((index), (word)), (value)); \
        CHECK(expect_status_unchanged(mutated, sizeof(mutated), (status), #word)); \
    } while (0)

    EXPECT_DIRECTORY_WORD(1, 0, 99, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY);
    EXPECT_DIRECTORY_WORD(1, 1, 2, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY);
    EXPECT_DIRECTORY_WORD(1, 3, 0, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY);
    EXPECT_DIRECTORY_WORD(1, 3, 2, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY);
    EXPECT_DIRECTORY_WORD(
        1,
        3,
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_BYTE_SIZE +
            ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT,
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY
    );
    EXPECT_DIRECTORY_WORD(
        1,
        2,
        transform_offset - ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT,
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY
    );
    EXPECT_DIRECTORY_WORD(
        1,
        2,
        transform_offset + ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT,
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY
    );
    EXPECT_DIRECTORY_WORD(
        1,
        2,
        transform_offset + UINT32_C(2),
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY
    );
    EXPECT_DIRECTORY_WORD(1, 4, 0, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY);
    EXPECT_DIRECTORY_WORD(1, 4, 2, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY);
    EXPECT_DIRECTORY_WORD(1, 5, 0, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY);
    EXPECT_DIRECTORY_WORD(1, 5, 2, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY);
    EXPECT_DIRECTORY_WORD(1, 6, 1, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY);
    EXPECT_DIRECTORY_WORD(1, 7, 1, ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY);
    EXPECT_DIRECTORY_WORD(
        5,
        4,
        ACGC_GX_CANONICAL_TEV_SECTION_COUNT_MIN - UINT32_C(1),
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY
    );
    EXPECT_DIRECTORY_WORD(
        5,
        4,
        ACGC_GX_CANONICAL_TEV_SECTION_COUNT_MAX + UINT32_C(1),
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY
    );

    /* Fog metadata is fixed by the common canonical envelope validator. */
    EXPECT_DIRECTORY_WORD(
        11,
        3,
        ACGC_GX_CANONICAL_FOG_STATE_SIZE +
            ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT,
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY
    );
    EXPECT_DIRECTORY_WORD(
        11,
        4,
        UINT32_C(2),
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY
    );
    EXPECT_DIRECTORY_WORD(
        11,
        5,
        UINT32_C(2),
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY
    );
    EXPECT_DIRECTORY_WORD(
        13,
        3,
        UINT32_C(8),
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY
    );
    EXPECT_DIRECTORY_WORD(
        13,
        3,
        UINT32_MAX - UINT32_C(3),
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY
    );

    uint8_t empty[ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET];
    build_empty_fixture(empty);
    put_le32(
        empty,
        directory_word_offset(4, 1),
        ACGC_GX_CANONICAL_SECTION_VERSION
    );
    CHECK(expect_status_unchanged(
        empty,
        sizeof(empty),
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY,
        "nonzero absent entry"
    ));

#undef EXPECT_DIRECTORY_WORD
    return 1;
}

static int test_extent_and_arithmetic_edges(void)
{
    uint8_t valid[TEST_FULL_SIZE + 1];
    build_full_fixture(valid);
    valid[TEST_FULL_SIZE] = 0xEE;
    CHECK(expect_status_unchanged(
        valid,
        sizeof(valid),
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_SIZE,
        "trailing byte"
    ));

    uint8_t overflow[TEST_FULL_SIZE];
    build_full_fixture(overflow);
    put_le32(overflow, 36, UINT32_MAX - UINT32_C(3));
    put_le32(overflow, 40, UINT32_MAX - UINT32_C(3));
    CHECK(expect_status_unchanged(
        overflow,
        sizeof(overflow),
        ACGC_APPLE_CANONICAL_ENVELOPE_OVERFLOW,
        "payload total overflow"
    ));

    if (SIZE_MAX > UINT32_MAX) {
        AcgcAppleCanonicalEnvelopeView output;
        uint8_t before[sizeof(output)];
        memset(&output, 0xA5, sizeof(output));
        memcpy(before, &output, sizeof(output));
        CHECK(
            acgc_apple_canonical_envelope_parse(
                valid,
                (size_t)UINT32_MAX + (size_t)1,
                &output
            ) == ACGC_APPLE_CANONICAL_ENVELOPE_OVERFLOW
        );
        CHECK(memcmp(before, &output, sizeof(output)) == 0);
    }
    return 1;
}

static int test_reject_output_alias(void)
{
    _Alignas(AcgcAppleCanonicalEnvelopeView)
        uint8_t backing[TEST_FULL_SIZE];
    uint8_t before[sizeof(backing)];
    build_full_fixture(backing);
    memcpy(before, backing, sizeof(backing));

    AcgcAppleCanonicalEnvelopeView* alias_output =
        (AcgcAppleCanonicalEnvelopeView*)(void*)backing;
    CHECK(
        acgc_apple_canonical_envelope_parse(
            backing,
            sizeof(backing),
            alias_output
        ) == ACGC_APPLE_CANONICAL_ENVELOPE_OUTPUT_OVERLAP
    );
    CHECK(memcmp(before, backing, sizeof(backing)) == 0);
    return 1;
}

static int test_invalid_arguments(void)
{
    AcgcAppleCanonicalEnvelopeView output;
    uint8_t before[sizeof(output)];
    uint8_t bytes[TEST_FULL_SIZE];
    build_full_fixture(bytes);
    memset(&output, 0xA5, sizeof(output));
    memcpy(before, &output, sizeof(output));

    CHECK(
        acgc_apple_canonical_envelope_parse(NULL, sizeof(bytes), &output) ==
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_ARGUMENT
    );
    CHECK(memcmp(before, &output, sizeof(output)) == 0);
    CHECK(
        acgc_apple_canonical_envelope_parse(bytes, sizeof(bytes), NULL) ==
        ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_ARGUMENT
    );
    CHECK(
        acgc_apple_canonical_envelope_status_string(
            (AcgcAppleCanonicalEnvelopeParserStatus)99
        ) != NULL
    );
    return 1;
}

int main(void)
{
    if (!test_golden_full_envelope() ||
        !test_golden_empty_envelope() ||
        !test_truncation_at_every_prefix() ||
        !test_invalid_header_values() ||
        !test_invalid_directory_values() ||
        !test_extent_and_arithmetic_edges() ||
        !test_reject_output_alias() ||
        !test_invalid_arguments()) {
        return 1;
    }
    puts("PASS acgc_apple_canonical_envelope_parser_fixture");
    return 0;
}
