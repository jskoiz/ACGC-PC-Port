#include "acgc/gx_canonical_raster_state.h"

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
