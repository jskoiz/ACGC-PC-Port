#include "acgc/gx_canonical_raster_state.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", \
                    __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static uint32_t read_le32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static void write_le32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & UINT32_C(0xFF));
    bytes[1] = (uint8_t)((value >> 8) & UINT32_C(0xFF));
    bytes[2] = (uint8_t)((value >> 16) & UINT32_C(0xFF));
    bytes[3] = (uint8_t)((value >> 24) & UINT32_C(0xFF));
}

static void encode_le_words(
    const AcgcGxCanonicalRasterState* state,
    uint8_t bytes[ACGC_GX_CANONICAL_RASTER_STATE_SIZE]
) {
    uint32_t words[ACGC_GX_CANONICAL_RASTER_STATE_WORD_COUNT];
    uint32_t index;

    memcpy(words, state, sizeof(words));
    for (index = 0; index < ACGC_GX_CANONICAL_RASTER_STATE_WORD_COUNT;
         index++) {
        write_le32(bytes + index * sizeof(uint32_t), words[index]);
    }
}

static void decode_le_words(
    AcgcGxCanonicalRasterState* state,
    const uint8_t bytes[ACGC_GX_CANONICAL_RASTER_STATE_SIZE]
) {
    uint32_t words[ACGC_GX_CANONICAL_RASTER_STATE_WORD_COUNT];
    uint32_t index;

    for (index = 0; index < ACGC_GX_CANONICAL_RASTER_STATE_WORD_COUNT;
         index++) {
        words[index] = read_le32(bytes + index * sizeof(uint32_t));
    }
    memcpy(state, words, sizeof(*state));
}

static void fill_valid_state(AcgcGxCanonicalRasterState* state) {
    memset(state, 0, sizeof(*state));
    state->viewport_bits[0] = UINT32_C(0x80000000); /* -0.0f */
    state->viewport_bits[1] = UINT32_C(0x3F800000); /* 1.0f */
    state->viewport_bits[2] = UINT32_C(0x40490FDB); /* pi, rounded */
    state->viewport_bits[3] = UINT32_C(0x00000001); /* positive subnormal */
    state->viewport_bits[4] = UINT32_C(0xBF800000); /* -1.0f */
    state->viewport_bits[5] = UINT32_C(0x7F7FFFFF); /* max finite */
    state->scissor[0] = 0;
    state->scissor[1] = 0;
    state->scissor[2] = 640;
    state->scissor[3] = 480;
    state->scissor_offset[0] = ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MIN;
    state->scissor_offset[1] = ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MAX;
    state->clip_mode = ACGC_GX_CANONICAL_RASTER_CLIP_MODE_DISABLE;
    state->cull_mode = ACGC_GX_CANONICAL_RASTER_CULL_MODE_ALL;
    state->co_planar_enable = 1;
    state->line_width = ACGC_GX_CANONICAL_RASTER_SIZE_MAX;
    state->line_tex_offsets = ACGC_GX_CANONICAL_RASTER_TEX_OFFSET_MAX;
    state->point_size = 0;
    state->point_tex_offsets = ACGC_GX_CANONICAL_RASTER_TEX_OFFSET_MIN;
    state->line_texcoord_mask = UINT32_C(0xA5);
    state->point_texcoord_mask = UINT32_C(0x5A);
    state->dither = 1;
    state->dst_alpha_enable = 1;
    state->dst_alpha = ACGC_GX_CANONICAL_RASTER_DST_ALPHA_MAX;
    state->field_mode = 1;
    state->half_aspect_ratio = 0;
    state->field_odd_mask = 1;
    state->field_even_mask = 0;
}

static AcgcGxCanonicalEnvelopeDirectoryEntry* raster_entry(
    AcgcGxCanonicalEnvelope* envelope
) {
    return &envelope->directory[
        ACGC_GX_CANONICAL_RASTER_SECTION_ID - 1];
}

static void prepare_raster_envelope(AcgcGxCanonicalEnvelope* envelope) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_RASTER_SECTION_MASK;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_RASTER_SECTION_MASK;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_RASTER_SECTION_BYTE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_RASTER_SECTION_BYTE_SIZE;

    entry = raster_entry(envelope);
    entry->section_version = ACGC_GX_CANONICAL_RASTER_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_RASTER_SECTION_BYTE_SIZE;
    entry->count = ACGC_GX_CANONICAL_RASTER_SECTION_COUNT;
    entry->capacity = ACGC_GX_CANONICAL_RASTER_SECTION_CAPACITY;
    entry->valid_mask = ACGC_GX_CANONICAL_RASTER_SECTION_MASK;
}

static int accepts_domain_boundaries(void) {
    AcgcGxCanonicalRasterState state;

    fill_valid_state(&state);
    CHECK(acgc_gx_canonical_raster_state_validate(&state));

    state.scissor[0] = ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT - 1;
    state.scissor[1] = ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT - 1;
    state.scissor[2] = 0;
    state.scissor[3] = 0;
    CHECK(acgc_gx_canonical_raster_state_validate(&state));
    return 1;
}

static int rejects_nonfinite_viewport_words(void) {
    AcgcGxCanonicalRasterState state;

    fill_valid_state(&state);
    CHECK(!acgc_gx_canonical_raster_state_validate(NULL));

    state.viewport_bits[0] = UINT32_C(0x7F800000);
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    state.viewport_bits[0] = UINT32_C(0xFF800001);
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    return 1;
}

static int rejects_domain_invalid_words(void) {
    AcgcGxCanonicalRasterState state;

    fill_valid_state(&state);
    state.scissor[0] = ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.scissor[2] = ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.scissor[2] = UINT32_MAX;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.scissor_offset[0] =
        ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MIN - 1;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.scissor_offset[1] =
        ACGC_GX_CANONICAL_RASTER_SCISSOR_OFFSET_MAX + 1;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.clip_mode = 2;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.cull_mode = 4;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.co_planar_enable = 2;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.line_width = ACGC_GX_CANONICAL_RASTER_SIZE_MAX + 1;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.point_size = ACGC_GX_CANONICAL_RASTER_SIZE_MAX + 1;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.line_tex_offsets = ACGC_GX_CANONICAL_RASTER_TEX_OFFSET_MAX + 1;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.point_tex_offsets = ACGC_GX_CANONICAL_RASTER_TEX_OFFSET_MAX + 1;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.line_texcoord_mask =
        ACGC_GX_CANONICAL_RASTER_TEXCOORD_MASK_MAX + 1;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.point_texcoord_mask =
        ACGC_GX_CANONICAL_RASTER_TEXCOORD_MASK_MAX + 1;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.dither = 2;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.dst_alpha_enable = 2;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.dst_alpha = ACGC_GX_CANONICAL_RASTER_DST_ALPHA_MAX + 1;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.field_mode = 2;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.half_aspect_ratio = 2;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.field_odd_mask = 2;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    fill_valid_state(&state);
    state.field_even_mask = 2;
    CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    return 1;
}

static int rejects_nonzero_reserved_words(void) {
    AcgcGxCanonicalRasterState state;
    uint32_t index;

    for (index = 0;
         index < ACGC_GX_CANONICAL_RASTER_RESERVED_WORD_COUNT;
         index++) {
        fill_valid_state(&state);
        state.reserved[index] = 1;
        CHECK(!acgc_gx_canonical_raster_state_validate(&state));
    }
    return 1;
}

static int preserves_exact_little_endian_roundtrip(void) {
    AcgcGxCanonicalRasterState first;
    AcgcGxCanonicalRasterState second;
    uint8_t first_bytes[ACGC_GX_CANONICAL_RASTER_STATE_SIZE];
    uint8_t second_bytes[ACGC_GX_CANONICAL_RASTER_STATE_SIZE];

    fill_valid_state(&first);
    CHECK(acgc_gx_canonical_raster_state_validate(&first));
    encode_le_words(&first, first_bytes);
    decode_le_words(&second, first_bytes);
    CHECK(acgc_gx_canonical_raster_state_validate(&second));
    CHECK(memcmp(&first, &second, sizeof(first)) == 0);
    encode_le_words(&second, second_bytes);
    CHECK(memcmp(first_bytes, second_bytes, sizeof(first_bytes)) == 0);

    CHECK(read_le32(first_bytes + ACGC_GX_CANONICAL_RASTER_VIEWPORT_OFFSET) ==
          UINT32_C(0x80000000));
    CHECK(read_le32(first_bytes + ACGC_GX_CANONICAL_RASTER_SCISSOR_BOX_OFFSET_OFFSET) ==
          UINT32_C(0xFFFFFEAA));
    CHECK(read_le32(first_bytes + ACGC_GX_CANONICAL_RASTER_RESERVED_OFFSET) == 0);
    CHECK(first_bytes[ACGC_GX_CANONICAL_RASTER_VIEWPORT_OFFSET + 0] == 0x00);
    CHECK(first_bytes[ACGC_GX_CANONICAL_RASTER_VIEWPORT_OFFSET + 1] == 0x00);
    CHECK(first_bytes[ACGC_GX_CANONICAL_RASTER_VIEWPORT_OFFSET + 2] == 0x00);
    CHECK(first_bytes[ACGC_GX_CANONICAL_RASTER_VIEWPORT_OFFSET + 3] == 0x80);
    return 1;
}

static int accepts_exact_raster_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_raster_envelope(&envelope);
    entry = raster_entry(&envelope);
    CHECK(acgc_gx_canonical_raster_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(entry->section_id == ACGC_GX_CANONICAL_RASTER_SECTION_ID);
    CHECK(entry->section_version == 1);
    CHECK(entry->byte_size == 128);
    CHECK(entry->count == 1);
    CHECK(entry->capacity == 1);
    CHECK(entry->valid_mask == UINT32_C(0x0400));
    CHECK(entry->reserved == 0);
    return 1;
}

static int rejects_non_exact_raster_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_raster_envelope(&envelope);
    entry = raster_entry(&envelope);
    entry->section_version = 2;
    CHECK(!acgc_gx_canonical_raster_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_raster_envelope(&envelope);
    entry = raster_entry(&envelope);
    entry->byte_size = 124;
    envelope.header.payload_byte_size = 124;
    envelope.header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + 124;
    CHECK(!acgc_gx_canonical_raster_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_raster_envelope(&envelope);
    entry = raster_entry(&envelope);
    entry->valid_mask = ACGC_GX_CANONICAL_SECTION_MASK_DEPTH;
    CHECK(!acgc_gx_canonical_raster_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    return 1;
}

static int accepts_absent_raster_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    CHECK(acgc_gx_canonical_raster_metadata_validate(
        &envelope, sizeof(envelope)));
    return 1;
}

int main(void) {
    CHECK(accepts_domain_boundaries());
    CHECK(rejects_nonfinite_viewport_words());
    CHECK(rejects_domain_invalid_words());
    CHECK(rejects_nonzero_reserved_words());
    CHECK(preserves_exact_little_endian_roundtrip());
    CHECK(accepts_exact_raster_metadata());
    CHECK(rejects_non_exact_raster_metadata());
    CHECK(accepts_absent_raster_metadata());
    return 0;
}
