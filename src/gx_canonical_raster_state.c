#include "acgc/gx_canonical_raster_state.h"

#include <string.h>

static int canonical_raster_binary32_is_finite(uint32_t bits) {
    return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

static int canonical_raster_word_is_bounded(
    uint32_t value,
    uint32_t minimum,
    uint32_t maximum
) {
    return value >= minimum && value <= maximum;
}

static int canonical_raster_words_are_zero(
    const uint32_t* words,
    uint32_t count
) {
    uint32_t index;

    for (index = 0; index < count; index++) {
        if (words[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int canonical_raster_viewport_is_valid(
    const AcgcGxCanonicalRasterState* state
) {
    uint32_t index;

    for (index = 0;
         index < ACGC_GX_CANONICAL_RASTER_VIEWPORT_WORD_COUNT;
         index++) {
        if (!canonical_raster_binary32_is_finite(
                state->viewport_bits[index])) {
            return 0;
        }
    }
    return 1;
}

static int canonical_raster_scissor_is_valid(
    const AcgcGxCanonicalRasterState* state
) {
    const uint32_t left = state->scissor[0];
    const uint32_t top = state->scissor[1];
    const uint32_t width = state->scissor[2];
    const uint32_t height = state->scissor[3];

    /* Match GXSetScissor's strict < 1706 origin and edge domain without
     * allowing a u32 addition to wrap around into an apparent valid value. */
    return left < ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT &&
        top < ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT &&
        width < ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT - left &&
        height < ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT - top;
}

static int canonical_raster_scissor_offsets_are_valid(
    const AcgcGxCanonicalRasterState* state
) {
    return state->scissor_offset[0] >=
            ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MIN &&
        state->scissor_offset[0] <=
            ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MAX &&
        state->scissor_offset[1] >=
            ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MIN &&
        state->scissor_offset[1] <=
            ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MAX;
}

int acgc_gx_canonical_raster_state_validate(
    const AcgcGxCanonicalRasterState* state
) {
    if (state == NULL ||
        !canonical_raster_viewport_is_valid(state) ||
        !canonical_raster_scissor_is_valid(state) ||
        !canonical_raster_scissor_offsets_are_valid(state) ||
        !canonical_raster_word_is_bounded(
            state->clip_mode,
            ACGC_GX_CANONICAL_RASTER_CLIP_MODE_ENABLE,
            ACGC_GX_CANONICAL_RASTER_CLIP_MODE_DISABLE) ||
        !canonical_raster_word_is_bounded(
            state->cull_mode,
            ACGC_GX_CANONICAL_RASTER_CULL_MODE_NONE,
            ACGC_GX_CANONICAL_RASTER_CULL_MODE_ALL) ||
        !canonical_raster_word_is_bounded(
            state->co_planar_enable,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX) ||
        !canonical_raster_word_is_bounded(
            state->line_width,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_RASTER_SIZE_MAX) ||
        !canonical_raster_word_is_bounded(
            state->point_size,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_RASTER_SIZE_MAX) ||
        !canonical_raster_word_is_bounded(
            state->line_tex_offsets,
            ACGC_GX_CANONICAL_RASTER_TEX_OFFSET_MIN,
            ACGC_GX_CANONICAL_RASTER_TEX_OFFSET_MAX) ||
        !canonical_raster_word_is_bounded(
            state->point_tex_offsets,
            ACGC_GX_CANONICAL_RASTER_TEX_OFFSET_MIN,
            ACGC_GX_CANONICAL_RASTER_TEX_OFFSET_MAX) ||
        state->line_texcoord_mask >
            ACGC_GX_CANONICAL_RASTER_TEXCOORD_MASK_MAX ||
        state->point_texcoord_mask >
            ACGC_GX_CANONICAL_RASTER_TEXCOORD_MASK_MAX ||
        !canonical_raster_word_is_bounded(
            state->dither,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX) ||
        !canonical_raster_word_is_bounded(
            state->dst_alpha_enable,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX) ||
        state->dst_alpha > ACGC_GX_CANONICAL_RASTER_DST_ALPHA_MAX ||
        !canonical_raster_word_is_bounded(
            state->field_mode,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX) ||
        !canonical_raster_word_is_bounded(
            state->half_aspect_ratio,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX) ||
        !canonical_raster_word_is_bounded(
            state->field_odd_mask,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX) ||
        !canonical_raster_word_is_bounded(
            state->field_even_mask,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MIN,
            ACGC_GX_CANONICAL_RASTER_BOOLEAN_MAX) ||
        !canonical_raster_words_are_zero(
            state->reserved,
            ACGC_GX_CANONICAL_RASTER_RESERVED_WORD_COUNT)) {
        return 0;
    }
    return 1;
}

static void canonical_raster_write_le32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)(value & UINT32_C(0xFF));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    destination[3] = (uint8_t)((value >> 24) & UINT32_C(0xFF));
}

static void canonical_raster_encode_word(
    uint8_t* destination,
    size_t* offset,
    uint32_t value
) {
    canonical_raster_write_le32(destination + *offset, value);
    *offset += sizeof(uint32_t);
}

int acgc_gx_canonical_raster_state_encode(
    const AcgcGxCanonicalRasterState* state,
    uint8_t* destination,
    size_t destination_byte_size
) {
    uint8_t encoded[ACGC_GX_CANONICAL_RASTER_STATE_SIZE];
    size_t offset = 0;
    size_t index;
    uint32_t word;

    if (state == NULL || destination == NULL ||
        destination_byte_size != ACGC_GX_CANONICAL_RASTER_STATE_SIZE ||
        !acgc_gx_canonical_raster_state_validate(state)) {
        return 0;
    }

    memset(encoded, 0, sizeof(encoded));
    for (word = 0; word < ACGC_GX_CANONICAL_RASTER_VIEWPORT_WORD_COUNT;
         word++) {
        canonical_raster_encode_word(
            encoded, &offset, state->viewport_bits[word]);
    }
    for (word = 0; word < ACGC_GX_CANONICAL_RASTER_SCISSOR_WORD_COUNT;
         word++) {
        canonical_raster_encode_word(encoded, &offset, state->scissor[word]);
    }
    for (word = 0; word < ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_COUNT;
         word++) {
        canonical_raster_encode_word(
            encoded, &offset, (uint32_t)state->scissor_offset[word]);
    }
    canonical_raster_encode_word(encoded, &offset, state->clip_mode);
    canonical_raster_encode_word(encoded, &offset, state->cull_mode);
    canonical_raster_encode_word(encoded, &offset, state->co_planar_enable);
    canonical_raster_encode_word(encoded, &offset, state->line_width);
    canonical_raster_encode_word(encoded, &offset, state->line_tex_offsets);
    canonical_raster_encode_word(encoded, &offset, state->point_size);
    canonical_raster_encode_word(encoded, &offset, state->point_tex_offsets);
    canonical_raster_encode_word(encoded, &offset, state->line_texcoord_mask);
    canonical_raster_encode_word(encoded, &offset, state->point_texcoord_mask);
    canonical_raster_encode_word(encoded, &offset, state->dither);
    canonical_raster_encode_word(
        encoded, &offset, state->dst_alpha_enable);
    canonical_raster_encode_word(encoded, &offset, state->dst_alpha);
    canonical_raster_encode_word(encoded, &offset, state->field_mode);
    canonical_raster_encode_word(
        encoded, &offset, state->half_aspect_ratio);
    canonical_raster_encode_word(encoded, &offset, state->field_odd_mask);
    canonical_raster_encode_word(encoded, &offset, state->field_even_mask);
    for (word = 0; word < ACGC_GX_CANONICAL_RASTER_RESERVED_WORD_COUNT;
         word++) {
        canonical_raster_encode_word(encoded, &offset, state->reserved[word]);
    }
    if (offset != ACGC_GX_CANONICAL_RASTER_STATE_SIZE) {
        return 0;
    }
    for (index = 0; index < sizeof(encoded); index++) {
        destination[index] = encoded[index];
    }
    return 1;
}

static int canonical_raster_entry_is_absent(
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry
) {
    return entry != NULL &&
        entry->section_id == ACGC_GX_CANONICAL_RASTER_SECTION_ID &&
        entry->section_version == 0 &&
        entry->byte_offset == 0 &&
        entry->byte_size == 0 &&
        entry->count == 0 &&
        entry->capacity == 0 &&
        entry->valid_mask == 0 &&
        entry->reserved == 0;
}

int acgc_gx_canonical_raster_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
) {
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    if (!acgc_gx_canonical_envelope_validate(
            envelope, envelope_byte_size)) {
        return 0;
    }

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_RASTER_SECTION_ID - 1];
    if ((envelope->header.present_state_mask &
            ACGC_GX_CANONICAL_RASTER_SECTION_MASK) == 0) {
        return canonical_raster_entry_is_absent(entry);
    }

    return entry->section_id == ACGC_GX_CANONICAL_RASTER_SECTION_ID &&
        entry->section_version == ACGC_GX_CANONICAL_RASTER_SECTION_VERSION &&
        entry->byte_size == ACGC_GX_CANONICAL_RASTER_SECTION_BYTE_SIZE &&
        entry->count == ACGC_GX_CANONICAL_RASTER_SECTION_COUNT &&
        entry->capacity == ACGC_GX_CANONICAL_RASTER_SECTION_CAPACITY &&
        entry->valid_mask == ACGC_GX_CANONICAL_RASTER_SECTION_MASK &&
        entry->reserved == 0;
}
