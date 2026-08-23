#include "acgc/apple_canonical_envelope_parser.h"

#include "acgc/gx_canonical_state.h"

#include <stdint.h>
#include <string.h>

static int apple_size_add(size_t left, size_t right, size_t* result)
{
    if (right > SIZE_MAX - left) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int apple_size_mul(size_t left, size_t right, size_t* result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return 0;
    }
    *result = left * right;
    return 1;
}

static int apple_uintptr_range_end(
    uintptr_t start,
    size_t length,
    uintptr_t* end
)
{
    if ((uintmax_t)length >
        (uintmax_t)UINTPTR_MAX - (uintmax_t)start) {
        return 0;
    }
    *end = start + (uintptr_t)length;
    return 1;
}

static AcgcAppleCanonicalEnvelopeParserStatus apple_validate_disjoint_ranges(
    const uint8_t* bytes,
    size_t byte_size,
    const AcgcAppleCanonicalEnvelopeView* output
)
{
    const uintptr_t input_start = (uintptr_t)(const void*)bytes;
    const uintptr_t output_start = (uintptr_t)(const void*)output;
    uintptr_t input_end = 0;
    uintptr_t output_end = 0;
    if (!apple_uintptr_range_end(input_start, byte_size, &input_end) ||
        !apple_uintptr_range_end(
            output_start,
            sizeof(*output),
            &output_end
        )) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_OVERFLOW;
    }
    if (byte_size != 0 &&
        input_start < output_end && output_start < input_end) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_OUTPUT_OVERLAP;
    }
    return ACGC_APPLE_CANONICAL_ENVELOPE_OK;
}

static int apple_read_le32(
    const uint8_t* bytes,
    size_t byte_size,
    size_t offset,
    uint32_t* value
)
{
    if (offset > byte_size || byte_size - offset < sizeof(uint32_t)) {
        return 0;
    }

    *value = (uint32_t)bytes[offset] |
        ((uint32_t)bytes[offset + 1] << 8) |
        ((uint32_t)bytes[offset + 2] << 16) |
        ((uint32_t)bytes[offset + 3] << 24);
    return 1;
}

static uint32_t apple_expected_section_id(size_t index)
{
    static const uint32_t ids[
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

    return ids[index];
}

static AcgcAppleCanonicalEnvelopeParserStatus apple_read_header(
    const uint8_t* bytes,
    size_t byte_size,
    AcgcAppleCanonicalEnvelopeView* parsed
)
{
    uint32_t* header_words[] = {
        &parsed->magic,
        &parsed->version,
        &parsed->header_byte_size,
        &parsed->directory_entry_byte_size,
        &parsed->directory_count,
        &parsed->known_state_mask,
        &parsed->present_state_mask,
        &parsed->required_state_mask,
        &parsed->payload_offset,
        &parsed->payload_byte_size,
        &parsed->total_byte_size,
        &parsed->reserved
    };

    for (size_t word_index = 0; word_index < 12; ++word_index) {
        size_t word_offset = 0;
        if (!apple_size_mul(word_index, sizeof(uint32_t), &word_offset) ||
            !apple_read_le32(
                bytes,
                byte_size,
                word_offset,
                header_words[word_index]
            )) {
            return ACGC_APPLE_CANONICAL_ENVELOPE_TRUNCATED;
        }
    }

    if (parsed->magic != ACGC_GX_CANONICAL_ENVELOPE_MAGIC ||
        parsed->header_byte_size !=
            ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE ||
        parsed->directory_entry_byte_size !=
            ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE ||
        parsed->directory_count !=
            ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER;
    }
    if (parsed->version != ACGC_GX_CANONICAL_ENVELOPE_VERSION) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_UNSUPPORTED_VERSION;
    }
    if (parsed->known_state_mask !=
            ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK ||
        (parsed->present_state_mask &
            ~ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK) != 0 ||
        (parsed->required_state_mask &
            ~ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK) != 0 ||
        (parsed->required_state_mask & ~parsed->present_state_mask) != 0 ||
        parsed->reserved != 0) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER;
    }
    if (parsed->payload_offset !=
            ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET ||
        (parsed->payload_offset % ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT) != 0 ||
        (parsed->payload_byte_size % ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT) != 0 ||
        (parsed->total_byte_size % ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT) != 0) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER;
    }

    const uint64_t expected_total =
        (uint64_t)parsed->payload_offset +
        (uint64_t)parsed->payload_byte_size;
    if (expected_total > (uint64_t)UINT32_MAX) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_OVERFLOW;
    }
    if (parsed->total_byte_size != (uint32_t)expected_total) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_SIZE;
    }
    if ((parsed->present_state_mask == 0) !=
        (parsed->payload_byte_size == 0)) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER;
    }
    if ((uint64_t)byte_size < expected_total) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_TRUNCATED;
    }
    if ((uint64_t)byte_size > expected_total) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_SIZE;
    }

    return ACGC_APPLE_CANONICAL_ENVELOPE_OK;
}

static AcgcAppleCanonicalEnvelopeParserStatus apple_read_directory(
    const uint8_t* bytes,
    size_t byte_size,
    AcgcAppleCanonicalEnvelopeView* parsed
)
{
    size_t cursor = (size_t)ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;

    for (size_t index = 0;
         index < ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT;
         ++index) {
        size_t relative_offset = 0;
        size_t entry_offset = 0;
        if (!apple_size_mul(
                index,
                (size_t)ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE,
                &relative_offset) ||
            !apple_size_add(
                (size_t)ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_OFFSET,
                relative_offset,
                &entry_offset)) {
            return ACGC_APPLE_CANONICAL_ENVELOPE_OVERFLOW;
        }

        uint32_t words[8] = {0};
        for (size_t word_index = 0; word_index < 8; ++word_index) {
            size_t word_relative_offset = 0;
            size_t word_offset = 0;
            if (!apple_size_mul(
                    word_index,
                    sizeof(uint32_t),
                    &word_relative_offset
                ) ||
                !apple_size_add(
                    entry_offset,
                    word_relative_offset,
                    &word_offset
                ) ||
                !apple_read_le32(
                    bytes,
                    byte_size,
                    word_offset,
                    &words[word_index]
                )) {
                return ACGC_APPLE_CANONICAL_ENVELOPE_TRUNCATED;
            }
        }

        const uint32_t expected_id = apple_expected_section_id(index);
        const uint32_t expected_mask = UINT32_C(1) << index;
        if (words[0] != expected_id) {
            return ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY;
        }

        AcgcAppleCanonicalEnvelopeSection section = {
            words[0],
            words[1],
            words[2],
            words[3],
            words[4],
            words[5],
            words[6],
            words[7]
        };
        if ((parsed->present_state_mask & expected_mask) == 0) {
            if (words[1] != 0 || words[2] != 0 || words[3] != 0 ||
                words[4] != 0 || words[5] != 0 || words[6] != 0 ||
                words[7] != 0) {
                return ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY;
            }
            parsed->sections[index] = section;
            continue;
        }

        if (words[1] != ACGC_GX_CANONICAL_SECTION_VERSION ||
            words[2] % ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT != 0 ||
            words[3] == 0 ||
            words[3] % ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT != 0 ||
            words[4] == 0 || words[5] == 0 || words[4] > words[5] ||
            words[6] != expected_mask || words[7] != 0) {
            return ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY;
        }
        if (words[2] != (uint32_t)cursor) {
            return ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY;
        }

        const uint64_t end = (uint64_t)words[2] + (uint64_t)words[3];
        if (end > (uint64_t)UINT32_MAX) {
            return ACGC_APPLE_CANONICAL_ENVELOPE_OVERFLOW;
        }
        if (end > (uint64_t)parsed->total_byte_size) {
            return ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY;
        }
        if (!apple_size_add(cursor, (size_t)words[3], &cursor)) {
            return ACGC_APPLE_CANONICAL_ENVELOPE_OVERFLOW;
        }
        parsed->sections[index] = section;
    }

    if (cursor != (size_t)parsed->total_byte_size) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY;
    }
    return ACGC_APPLE_CANONICAL_ENVELOPE_OK;
}

AcgcAppleCanonicalEnvelopeParserStatus
acgc_apple_canonical_envelope_parse(
    const uint8_t* bytes,
    size_t byte_size,
    AcgcAppleCanonicalEnvelopeView* output
)
{
    if (bytes == NULL || output == NULL) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_ARGUMENT;
    }
    if ((uintmax_t)byte_size > (uintmax_t)UINT32_MAX) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_OVERFLOW;
    }
    AcgcAppleCanonicalEnvelopeParserStatus status =
        apple_validate_disjoint_ranges(bytes, byte_size, output);
    if (status != ACGC_APPLE_CANONICAL_ENVELOPE_OK) {
        return status;
    }
    if (byte_size < (size_t)ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE) {
        return ACGC_APPLE_CANONICAL_ENVELOPE_TRUNCATED;
    }

    AcgcAppleCanonicalEnvelopeView parsed;
    memset(&parsed, 0, sizeof(parsed));

    status = apple_read_header(bytes, byte_size, &parsed);
    if (status != ACGC_APPLE_CANONICAL_ENVELOPE_OK) {
        return status;
    }
    status = apple_read_directory(bytes, byte_size, &parsed);
    if (status != ACGC_APPLE_CANONICAL_ENVELOPE_OK) {
        return status;
    }

    *output = parsed;
    return ACGC_APPLE_CANONICAL_ENVELOPE_OK;
}

const char* acgc_apple_canonical_envelope_status_string(
    AcgcAppleCanonicalEnvelopeParserStatus status
)
{
    switch (status) {
        case ACGC_APPLE_CANONICAL_ENVELOPE_OK:
            return "ok";
        case ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_ARGUMENT:
            return "invalid argument";
        case ACGC_APPLE_CANONICAL_ENVELOPE_OUTPUT_OVERLAP:
            return "output overlaps input";
        case ACGC_APPLE_CANONICAL_ENVELOPE_TRUNCATED:
            return "truncated";
        case ACGC_APPLE_CANONICAL_ENVELOPE_UNSUPPORTED_VERSION:
            return "unsupported version";
        case ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_HEADER:
            return "invalid header";
        case ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_SIZE:
            return "invalid size";
        case ACGC_APPLE_CANONICAL_ENVELOPE_INVALID_DIRECTORY:
            return "invalid directory";
        case ACGC_APPLE_CANONICAL_ENVELOPE_OVERFLOW:
            return "overflow";
    }
    return "unknown status";
}
