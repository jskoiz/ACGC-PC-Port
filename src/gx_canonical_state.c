#include "acgc/gx_canonical_state.h"

#include <string.h>

static int canonical_fog_type_is_valid(uint32_t fog_type) {
    switch (fog_type) {
        case ACGC_GX_CANONICAL_FOG_TYPE_NONE:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_LIN:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_EXP:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_EXP2:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_REVEXP:
        case ACGC_GX_CANONICAL_FOG_TYPE_PERSP_REVEXP2:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_LIN:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_EXP:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_EXP2:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_REVEXP:
        case ACGC_GX_CANONICAL_FOG_TYPE_ORTHO_REVEXP2:
            return 1;
        default:
            return 0;
    }
}

static int binary32_is_finite(uint32_t bits) {
    return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

/* Compare finite IEEE-754 binary32 values without converting through host
 * floating-point types.  This keeps validation defined by the wire bits. */
static int binary32_less(uint32_t lhs, uint32_t rhs) {
    const uint32_t lhs_magnitude = lhs & UINT32_C(0x7FFFFFFF);
    const uint32_t rhs_magnitude = rhs & UINT32_C(0x7FFFFFFF);
    const int lhs_negative = (lhs & UINT32_C(0x80000000)) != 0;
    const int rhs_negative = (rhs & UINT32_C(0x80000000)) != 0;

    if (lhs_magnitude == 0 && rhs_magnitude == 0) {
        return 0;
    }
    if (lhs_negative != rhs_negative) {
        return lhs_negative;
    }
    if (lhs_negative) {
        return lhs_magnitude > rhs_magnitude;
    }
    return lhs_magnitude < rhs_magnitude;
}

static int canonical_reserved_words_are_zero(
    const AcgcGxCanonicalFogState* state
) {
    uint32_t index;

    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RESERVED_WORD_COUNT; index++) {
        if (state->reserved[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int canonical_range_words_are_valid(
    const AcgcGxCanonicalFogState* state
) {
    uint32_t index;

    for (index = 0; index < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; index++) {
        if ((state->range_adjust[index] &
             ~ACGC_GX_CANONICAL_FOG_RANGE_VALUE_MASK) != 0) {
            return 0;
        }
    }
    return 1;
}

int acgc_gx_canonical_fog_state_validate(
    const AcgcGxCanonicalFogState* state
) {
    const int fog_is_active = state != NULL &&
        state->fog_type != ACGC_GX_CANONICAL_FOG_TYPE_NONE;
    const int fog_parameters_are_finite = state != NULL &&
        binary32_is_finite(state->start_bits) &&
        binary32_is_finite(state->end_bits) &&
        binary32_is_finite(state->near_bits) &&
        binary32_is_finite(state->far_bits);

    if (state == NULL ||
        !canonical_fog_type_is_valid(state->fog_type) ||
        state->range_adjust_enable > 1 ||
        state->range_center > ACGC_GX_CANONICAL_FOG_CENTER_SOURCE_MAX ||
        !canonical_range_words_are_valid(state) ||
        !canonical_reserved_words_are_zero(state)) {
        return 0;
    }

    /*
     * The decomp packs center + 342 into a ten-bit field.  A disabled range
     * adjustment retains the caller's widened u16 center without applying
     * that active-register limit; the range words remain structurally
     * bounded even while inactive.
     */
    if (state->range_adjust_enable != 0 &&
        state->range_center > ACGC_GX_CANONICAL_FOG_CENTER_MAX) {
        return 0;
    }

    /* GX_FOG_NONE disables fog math.  Its parameter words are intentionally
     * inactive and are not rewritten or interpreted by this validator. */
    if (fog_is_active && !fog_parameters_are_finite) {
        return 0;
    }

    /* The upstream GXSetFog assertions apply whenever these finite values
     * are supplied, including a disabled state. */
    if (binary32_is_finite(state->far_bits) &&
        binary32_less(state->far_bits, UINT32_C(0))) {
        return 0;
    }
    if (binary32_is_finite(state->far_bits) &&
        binary32_is_finite(state->near_bits) &&
        binary32_less(state->far_bits, state->near_bits)) {
        return 0;
    }

    /*
     * Do not reject far == near or end == start.  The decomp GXSetFog path
     * deliberately uses A=0, B=0.5, C=0 for either denominator-degenerate
     * case; a later renderer may apply that fallback without mutating this
     * value-only state.
     */
    return 1;
}

static void canonical_fog_write_le32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)(value & UINT32_C(0xFF));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    destination[3] = (uint8_t)((value >> 24) & UINT32_C(0xFF));
}

static void canonical_fog_encode_word(
    uint8_t* destination,
    size_t* offset,
    uint32_t value
) {
    canonical_fog_write_le32(destination + *offset, value);
    *offset += sizeof(uint32_t);
}

int acgc_gx_canonical_fog_state_encode(
    const AcgcGxCanonicalFogState* state,
    uint8_t* destination,
    size_t destination_byte_size
) {
    uint8_t encoded[ACGC_GX_CANONICAL_FOG_STATE_SIZE];
    size_t offset = 0;
    size_t index;
    uint32_t word;

    if (state == NULL || destination == NULL ||
        destination_byte_size != ACGC_GX_CANONICAL_FOG_STATE_SIZE ||
        !acgc_gx_canonical_fog_state_validate(state)) {
        return 0;
    }

    memset(encoded, 0, sizeof(encoded));
    canonical_fog_encode_word(encoded, &offset, state->fog_type);
    canonical_fog_encode_word(encoded, &offset, state->start_bits);
    canonical_fog_encode_word(encoded, &offset, state->end_bits);
    canonical_fog_encode_word(encoded, &offset, state->near_bits);
    canonical_fog_encode_word(encoded, &offset, state->far_bits);
    canonical_fog_encode_word(encoded, &offset, state->color_rgba8);
    canonical_fog_encode_word(
        encoded, &offset, state->range_adjust_enable);
    canonical_fog_encode_word(encoded, &offset, state->range_center);
    for (word = 0; word < ACGC_GX_CANONICAL_FOG_RANGE_COUNT; word++) {
        canonical_fog_encode_word(
            encoded, &offset, state->range_adjust[word]);
    }
    for (word = 0; word < ACGC_GX_CANONICAL_FOG_RESERVED_WORD_COUNT; word++) {
        canonical_fog_encode_word(encoded, &offset, state->reserved[word]);
    }
    if (offset != ACGC_GX_CANONICAL_FOG_STATE_SIZE) {
        return 0;
    }
    for (index = 0; index < sizeof(encoded); index++) {
        destination[index] = encoded[index];
    }
    return 1;
}

static uint32_t canonical_section_mask_for_id(uint32_t section_id) {
    switch (section_id) {
        case ACGC_GX_CANONICAL_SECTION_ID_GEOMETRY:
            return ACGC_GX_CANONICAL_SECTION_MASK_GEOMETRY;
        case ACGC_GX_CANONICAL_SECTION_ID_TRANSFORMS:
            return ACGC_GX_CANONICAL_SECTION_MASK_TRANSFORMS;
        case ACGC_GX_CANONICAL_SECTION_ID_CHANNELS:
            return ACGC_GX_CANONICAL_SECTION_MASK_CHANNELS;
        case ACGC_GX_CANONICAL_SECTION_ID_TEXGENS:
            return ACGC_GX_CANONICAL_SECTION_MASK_TEXGENS;
        case ACGC_GX_CANONICAL_SECTION_ID_TEXTURES:
            return ACGC_GX_CANONICAL_SECTION_MASK_TEXTURES;
        case ACGC_GX_CANONICAL_SECTION_ID_TEV:
            return ACGC_GX_CANONICAL_SECTION_MASK_TEV;
        case ACGC_GX_CANONICAL_SECTION_ID_LIGHTING:
            return ACGC_GX_CANONICAL_SECTION_MASK_LIGHTING;
        case ACGC_GX_CANONICAL_SECTION_ID_BLEND:
            return ACGC_GX_CANONICAL_SECTION_MASK_BLEND;
        case ACGC_GX_CANONICAL_SECTION_ID_ALPHA:
            return ACGC_GX_CANONICAL_SECTION_MASK_ALPHA;
        case ACGC_GX_CANONICAL_SECTION_ID_DEPTH:
            return ACGC_GX_CANONICAL_SECTION_MASK_DEPTH;
        case ACGC_GX_CANONICAL_SECTION_ID_RASTER:
            return ACGC_GX_CANONICAL_SECTION_MASK_RASTER;
        case ACGC_GX_CANONICAL_SECTION_ID_FOG:
            return ACGC_GX_CANONICAL_SECTION_MASK_FOG;
        case ACGC_GX_CANONICAL_SECTION_ID_INDIRECT:
            return ACGC_GX_CANONICAL_SECTION_MASK_INDIRECT;
        case ACGC_GX_CANONICAL_SECTION_ID_DYNAMIC:
            return ACGC_GX_CANONICAL_SECTION_MASK_DYNAMIC;
        default:
            return 0;
    }
}

static int canonical_section_version_is_valid(
    uint32_t section_id,
    uint32_t section_version
) {
    return canonical_section_mask_for_id(section_id) != 0 &&
        section_version == ACGC_GX_CANONICAL_SECTION_VERSION;
}

int acgc_gx_canonical_envelope_init(AcgcGxCanonicalEnvelope* envelope) {
    uint32_t index;

    if (envelope == NULL) {
        return 0;
    }

    memset(envelope, 0, sizeof(*envelope));
    envelope->header.magic = ACGC_GX_CANONICAL_ENVELOPE_MAGIC;
    envelope->header.version = ACGC_GX_CANONICAL_ENVELOPE_VERSION;
    envelope->header.header_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE;
    envelope->header.directory_entry_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE;
    envelope->header.directory_count =
        ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT;
    envelope->header.known_state_mask =
        ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK;
    envelope->header.payload_offset =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;

    for (index = 0;
         index < ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT;
         index++) {
        envelope->directory[index].section_id = index + 1;
    }
    return 1;
}

int acgc_gx_canonical_envelope_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
) {
    const AcgcGxCanonicalEnvelopeHeader* header;
    uint64_t cursor;
    uint64_t total_byte_size;
    uint32_t computed_present_mask = 0;
    uint32_t index;

    if (envelope == NULL ||
        envelope_byte_size < sizeof(*envelope) ||
        envelope_byte_size > (size_t)UINT32_MAX) {
        return 0;
    }

    header = &envelope->header;
    total_byte_size = header->total_byte_size;
    if (header->magic != ACGC_GX_CANONICAL_ENVELOPE_MAGIC ||
        header->version != ACGC_GX_CANONICAL_ENVELOPE_VERSION ||
        header->header_byte_size != ACGC_GX_CANONICAL_ENVELOPE_HEADER_SIZE ||
        header->directory_entry_byte_size !=
            ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_ENTRY_SIZE ||
        header->directory_count != ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT ||
        header->known_state_mask != ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK ||
        (header->present_state_mask &
            ~ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK) != 0 ||
        (header->required_state_mask &
            ~ACGC_GX_CANONICAL_ENVELOPE_KNOWN_STATE_MASK) != 0 ||
        (header->required_state_mask & ~header->present_state_mask) != 0 ||
        header->payload_offset != ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET ||
        (header->payload_offset % ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT) != 0 ||
        (header->payload_byte_size % ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT) != 0 ||
        header->reserved != 0 ||
        total_byte_size < header->payload_offset ||
        total_byte_size !=
            (uint64_t)header->payload_offset + header->payload_byte_size ||
        (uint64_t)envelope_byte_size != total_byte_size ||
        (total_byte_size % ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT) != 0 ||
        ((header->present_state_mask == 0) !=
            (header->payload_byte_size == 0))) {
        return 0;
    }

    cursor = header->payload_offset;
    for (index = 0;
         index < ACGC_GX_CANONICAL_ENVELOPE_DIRECTORY_COUNT;
         index++) {
        const AcgcGxCanonicalEnvelopeDirectoryEntry* entry =
            &envelope->directory[index];
        const uint32_t expected_id = index + 1;
        const uint32_t expected_mask =
            canonical_section_mask_for_id(expected_id);
        const int present =
            (header->present_state_mask & expected_mask) != 0;

        /* The fixed directory is canonicalized by ascending section ID. */
        if (entry->section_id != expected_id || expected_mask == 0) {
            return 0;
        }

        if (!present) {
            if (entry->section_version != 0 ||
                entry->byte_offset != 0 ||
                entry->byte_size != 0 ||
                entry->count != 0 ||
                entry->capacity != 0 ||
                entry->valid_mask != 0 ||
                entry->reserved != 0) {
                return 0;
            }
            continue;
        }

        if (!canonical_section_version_is_valid(
                entry->section_id, entry->section_version) ||
            entry->byte_size == 0 ||
            (entry->byte_offset % ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT) != 0 ||
            (entry->byte_size % ACGC_GX_CANONICAL_ENVELOPE_ALIGNMENT) != 0 ||
            entry->count == 0 ||
            entry->capacity == 0 ||
            entry->count > entry->capacity ||
            entry->valid_mask != expected_mask ||
            entry->reserved != 0 ||
            (uint64_t)entry->byte_offset != cursor) {
            return 0;
        }

        if ((uint64_t)entry->byte_offset + entry->byte_size < cursor ||
            (uint64_t)entry->byte_offset + entry->byte_size > total_byte_size) {
            return 0;
        }

        if (entry->section_id == ACGC_GX_CANONICAL_SECTION_ID_FOG &&
            (entry->section_version != ACGC_GX_CANONICAL_SECTION_VERSION ||
             entry->byte_size != ACGC_GX_CANONICAL_FOG_STATE_SIZE ||
             entry->count != 1 ||
             entry->capacity != 1)) {
            return 0;
        }

        computed_present_mask |= expected_mask;
        cursor += entry->byte_size;
    }

    return computed_present_mask == header->present_state_mask &&
        cursor == total_byte_size;
}
