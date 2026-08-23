#include "acgc/gx_canonical_transform_state.h"

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

static const uint32_t k_finite_words[] = {
    UINT32_C(0x00000000),
    UINT32_C(0x80000000),
    UINT32_C(0x3F800000),
    UINT32_C(0xBF800000),
    UINT32_C(0x40000000),
    UINT32_C(0xC0000000),
    UINT32_C(0x3F000000),
    UINT32_C(0xBF000000),
    UINT32_C(0x40400000),
    UINT32_C(0xC0400000),
    UINT32_C(0x40800000),
    UINT32_C(0xC0800000),
    UINT32_C(0x41000000),
    UINT32_C(0xC1000000),
    UINT32_C(0x7F7FFFFF),
    UINT32_C(0xFF7FFFFF)
};

static uint32_t finite_word(uint32_t index) {
    return k_finite_words[index % (sizeof(k_finite_words) /
                                   sizeof(k_finite_words[0]))];
}

static int exact_slot(uint32_t logical_id) {
    uint32_t slot;

    for (slot = 0;
         slot < ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT;
         slot++) {
        if (logical_id == slot *
                ACGC_GX_CANONICAL_TRANSFORM_LOGICAL_POSITION_ID_STRIDE) {
            return (int)slot;
        }
    }
    return -1;
}

static int load_position_immediate(
    AcgcGxCanonicalTransformState* state,
    const uint32_t words[ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT],
    uint32_t logical_id
) {
    const int slot = exact_slot(logical_id);

    if (state == NULL || words == NULL || slot < 0) {
        return 0;
    }
    memcpy(
        state->position[slot],
        words,
        sizeof(state->position[slot])
    );
    state->known_mask |=
        ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK((uint32_t)slot);
    return 1;
}

static int load_normal_immediate(
    AcgcGxCanonicalTransformState* state,
    const uint32_t words[ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT],
    uint32_t logical_id
) {
    const int slot = exact_slot(logical_id);

    if (state == NULL || words == NULL || slot < 0) {
        return 0;
    }
    memcpy(
        state->normal[slot],
        words,
        sizeof(state->normal[slot])
    );
    state->known_mask |=
        ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK((uint32_t)slot);
    return 1;
}

static int load_position_indexed_resolved(
    AcgcGxCanonicalTransformState* state,
    const uint32_t words[ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT],
    uint32_t logical_id
) {
    /* A resolved indexed load has no origin bit in the canonical payload. */
    return load_position_immediate(state, words, logical_id);
}

static int load_normal_indexed_resolved(
    AcgcGxCanonicalTransformState* state,
    const uint32_t words[ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT],
    uint32_t logical_id
) {
    return load_normal_immediate(state, words, logical_id);
}

static int load_position_indexed_unresolved(
    AcgcGxCanonicalTransformState* state,
    uint32_t logical_id
) {
    const int slot = exact_slot(logical_id);

    if (state == NULL || slot < 0) {
        return 0;
    }
    memset(state->position[slot], 0, sizeof(state->position[slot]));
    state->known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK((uint32_t)slot);
    return 1;
}

static int load_normal_indexed_unresolved(
    AcgcGxCanonicalTransformState* state,
    uint32_t logical_id
) {
    const int slot = exact_slot(logical_id);

    if (state == NULL || slot < 0) {
        return 0;
    }
    memset(state->normal[slot], 0, sizeof(state->normal[slot]));
    state->known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK((uint32_t)slot);
    return 1;
}

static void fill_state(
    AcgcGxCanonicalTransformState* state,
    uint32_t projection_type
) {
    uint32_t slot;
    uint32_t word;

    memset(state, 0, sizeof(*state));
    state->projection_type = projection_type;
    state->projection[0] = UINT32_C(0x3F800000);
    state->projection[1] = UINT32_C(0xBF800000);
    state->projection[2] = UINT32_C(0x40000000);
    state->projection[3] = UINT32_C(0xC0000000);
    state->projection[4] = UINT32_C(0x40400000);
    state->projection[5] = UINT32_C(0xC0400000);
    state->known_mask =
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK |
        ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK;

    for (slot = 0;
         slot < ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT;
         slot++) {
        state->known_mask |=
            ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(slot) |
            ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(slot);
        for (word = 0;
             word < ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT;
             word++) {
            state->position[slot][word] = finite_word(slot * 3 + word);
        }
        for (word = 0;
             word < ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT;
             word++) {
            state->normal[slot][word] = finite_word(slot * 5 + word + 1);
        }
    }
    state->current_position_id =
        ACGC_GX_CANONICAL_TRANSFORM_LOGICAL_POSITION_ID_FIRST;
}

static void write_le_words(
    const AcgcGxCanonicalTransformState* state,
    uint8_t bytes[ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE]
) {
    uint32_t words[
        ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE / sizeof(uint32_t)];
    uint32_t index;

    memcpy(words, state, sizeof(words));
    for (index = 0; index < sizeof(words) / sizeof(words[0]); index++) {
        const uint32_t word = words[index];
        const size_t offset = (size_t)index * sizeof(uint32_t);

        bytes[offset + 0] = (uint8_t)(word & UINT32_C(0xFF));
        bytes[offset + 1] = (uint8_t)((word >> 8) & UINT32_C(0xFF));
        bytes[offset + 2] = (uint8_t)((word >> 16) & UINT32_C(0xFF));
        bytes[offset + 3] = (uint8_t)((word >> 24) & UINT32_C(0xFF));
    }
}

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) |
        ((uint32_t)source[3] << 24);
}

static AcgcGxCanonicalEnvelopeDirectoryEntry* transform_entry(
    AcgcGxCanonicalEnvelope* envelope
) {
    return &envelope->directory[
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_ID - 1];
}

static void prepare_transform_envelope(
    AcgcGxCanonicalEnvelope* envelope
) {
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    (void)acgc_gx_canonical_envelope_init(envelope);
    envelope->header.present_state_mask =
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_MASK;
    envelope->header.required_state_mask =
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_MASK;
    envelope->header.payload_byte_size =
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_BYTE_SIZE;
    envelope->header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET +
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_BYTE_SIZE;

    entry = transform_entry(envelope);
    entry->section_version = ACGC_GX_CANONICAL_TRANSFORM_SECTION_VERSION;
    entry->byte_offset = ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET;
    entry->byte_size = ACGC_GX_CANONICAL_TRANSFORM_SECTION_BYTE_SIZE;
    entry->count = ACGC_GX_CANONICAL_TRANSFORM_SECTION_COUNT;
    entry->capacity = ACGC_GX_CANONICAL_TRANSFORM_SECTION_CAPACITY;
    entry->valid_mask = ACGC_GX_CANONICAL_TRANSFORM_SECTION_MASK;
}

static int accepts_exact_layout_and_metadata(void) {
    AcgcGxCanonicalTransformState state;
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    CHECK(sizeof(AcgcGxCanonicalTransformState) == 888);
    CHECK(_Alignof(AcgcGxCanonicalTransformState) == 4);
    CHECK(offsetof(AcgcGxCanonicalTransformState, projection_type) == 0x000);
    CHECK(offsetof(AcgcGxCanonicalTransformState, projection) == 0x004);
    CHECK(offsetof(AcgcGxCanonicalTransformState, known_mask) == 0x01C);
    CHECK(offsetof(AcgcGxCanonicalTransformState, current_position_id) ==
          0x020);
    CHECK(offsetof(AcgcGxCanonicalTransformState, reserved) == 0x024);
    CHECK(offsetof(AcgcGxCanonicalTransformState, position) == 0x030);
    CHECK(offsetof(AcgcGxCanonicalTransformState, normal) == 0x210);
    CHECK(ACGC_GX_CANONICAL_TRANSFORM_END_OFFSET == 0x378);
    CHECK(ACGC_GX_CANONICAL_TRANSFORM_SECTION_ID == 2);
    CHECK(ACGC_GX_CANONICAL_TRANSFORM_SECTION_MASK == UINT32_C(0x0002));
    CHECK(ACGC_GX_CANONICAL_TRANSFORM_SECTION_VERSION == 1);
    CHECK(ACGC_GX_CANONICAL_TRANSFORM_SECTION_BYTE_SIZE == 888);
    CHECK(ACGC_GX_CANONICAL_TRANSFORM_SECTION_COUNT == 1);
    CHECK(ACGC_GX_CANONICAL_TRANSFORM_SECTION_CAPACITY == 1);
    CHECK(ACGC_GX_CANONICAL_TRANSFORM_STATE_ALIGNMENT == 4);

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    CHECK(acgc_gx_canonical_transform_state_validate(&state));

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_ORTHOGRAPHIC);
    CHECK(acgc_gx_canonical_transform_state_validate(&state));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    CHECK(acgc_gx_canonical_transform_metadata_validate(
        &envelope, sizeof(envelope)));
    prepare_transform_envelope(&envelope);
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(acgc_gx_canonical_transform_metadata_validate(
        &envelope, envelope.header.total_byte_size));
    entry = transform_entry(&envelope);
    CHECK(entry->section_id == 2);
    CHECK(entry->section_version == 1);
    CHECK(entry->byte_size == 888);
    CHECK(entry->count == 1);
    CHECK(entry->capacity == 1);
    CHECK(entry->valid_mask == UINT32_C(0x0002));
    CHECK(entry->reserved == 0);
    CHECK(entry->byte_offset % 4 == 0);
    return 1;
}

static int accepts_every_logical_slot_and_current_relation(void) {
    AcgcGxCanonicalTransformState state;
    uint32_t slot;

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    for (slot = 0;
         slot < ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT;
         slot++) {
        state.current_position_id = slot *
            ACGC_GX_CANONICAL_TRANSFORM_LOGICAL_POSITION_ID_STRIDE;
        CHECK(acgc_gx_canonical_transform_state_validate(&state));
        CHECK((state.known_mask &
               ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(slot)) != 0);
        CHECK((state.known_mask &
               ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(slot)) != 0);
    }
    CHECK(state.current_position_id ==
          ACGC_GX_CANONICAL_TRANSFORM_LOGICAL_POSITION_ID_LAST);
    return 1;
}

static int rejects_bad_masks_ids_and_relations(void) {
    AcgcGxCanonicalTransformState state;
    static const uint32_t malformed_ids[] = {
        UINT32_C(1), UINT32_C(2), UINT32_C(28), UINT32_C(0xFFFFFFFF)
    };
    uint32_t index;

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    state.known_mask |= UINT32_C(0x00400000);
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    state.reserved[0] = 1;
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    state.reserved[2] = UINT32_C(0xFFFFFFFF);
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    state.projection_type = 2;
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));

    for (index = 0;
         index < sizeof(malformed_ids) / sizeof(malformed_ids[0]);
         index++) {
        fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
        state.current_position_id = malformed_ids[index];
        CHECK(!acgc_gx_canonical_transform_state_validate(&state));
    }

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    state.known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK;
    state.current_position_id = 3;
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));

    /* An unresolved current reference cannot name an unknown position slot. */
    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    memset(state.position[4], 0, sizeof(state.position[4]));
    state.known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(4);
    state.current_position_id = 12;
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));
    return 1;
}

static int rejects_nonfinite_known_words(void) {
    AcgcGxCanonicalTransformState state;

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    state.projection[0] = UINT32_C(0x7F800000);
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_ORTHOGRAPHIC);
    state.position[3][0] = UINT32_C(0xFF800000);
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    state.normal[8][8] = UINT32_C(0x7FC00001);
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));
    return 1;
}

static int accepts_unknown_zeroing_and_rejects_nonzero_unknowns(void) {
    AcgcGxCanonicalTransformState state;

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    memset(state.position[5], 0, sizeof(state.position[5]));
    state.known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(5);
    memset(state.normal[7], 0, sizeof(state.normal[7]));
    state.known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(7);
    CHECK(acgc_gx_canonical_transform_state_validate(&state));

    state.position[5][0] = UINT32_C(0x3F800000);
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    memset(state.normal[7], 0, sizeof(state.normal[7]));
    state.known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(7);
    CHECK(acgc_gx_canonical_transform_state_validate(&state));
    state.normal[7][0] = UINT32_C(0x3F800000);
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    state.known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK;
    state.projection_type = 0;
    memset(state.projection, 0, sizeof(state.projection));
    CHECK(acgc_gx_canonical_transform_state_validate(&state));
    state.projection[2] = UINT32_C(0x3F800000);
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    state.known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK;
    state.projection_type =
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_ORTHOGRAPHIC;
    memset(state.projection, 0, sizeof(state.projection));
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    state.known_mask &=
        ~ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK;
    state.current_position_id = 0;
    CHECK(acgc_gx_canonical_transform_state_validate(&state));
    return 1;
}

static int accepts_immediate_and_resolved_indexed_equivalence(void) {
    AcgcGxCanonicalTransformState immediate;
    AcgcGxCanonicalTransformState indexed;
    uint8_t immediate_bytes[ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE];
    uint8_t indexed_bytes[ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE];
    uint32_t position_words[
        ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT];
    uint32_t normal_words[
        ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT];
    uint32_t slot;
    uint32_t word;

    memset(&immediate, 0, sizeof(immediate));
    memset(&indexed, 0, sizeof(indexed));
    immediate.projection_type =
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE;
    indexed.projection_type = immediate.projection_type;
    for (word = 0;
         word < ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_WORD_COUNT;
         word++) {
        immediate.projection[word] = finite_word(word + 2);
        indexed.projection[word] = immediate.projection[word];
    }
    immediate.known_mask = indexed.known_mask =
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK |
        ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK;

    for (slot = 0;
         slot < ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT;
         slot++) {
        for (word = 0;
             word < ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT;
             word++) {
            position_words[word] = finite_word(slot + word + 3);
        }
        for (word = 0;
             word < ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT;
             word++) {
            normal_words[word] = finite_word(slot + word + 5);
        }
        CHECK(load_position_immediate(
            &immediate,
            position_words,
            slot * ACGC_GX_CANONICAL_TRANSFORM_LOGICAL_POSITION_ID_STRIDE));
        CHECK(load_normal_immediate(
            &immediate,
            normal_words,
            slot * ACGC_GX_CANONICAL_TRANSFORM_LOGICAL_POSITION_ID_STRIDE));
        CHECK(load_position_indexed_resolved(
            &indexed,
            position_words,
            slot * ACGC_GX_CANONICAL_TRANSFORM_LOGICAL_POSITION_ID_STRIDE));
        CHECK(load_normal_indexed_resolved(
            &indexed,
            normal_words,
            slot * ACGC_GX_CANONICAL_TRANSFORM_LOGICAL_POSITION_ID_STRIDE));
    }
    immediate.current_position_id = indexed.current_position_id = 27;
    CHECK(acgc_gx_canonical_transform_state_validate(&immediate));
    CHECK(acgc_gx_canonical_transform_state_validate(&indexed));
    write_le_words(&immediate, immediate_bytes);
    write_le_words(&indexed, indexed_bytes);
    CHECK(memcmp(immediate_bytes, indexed_bytes, sizeof(immediate_bytes)) == 0);

    /* Malformed load IDs are rejected without flooring into a valid slot. */
    immediate = indexed;
    CHECK(!load_position_immediate(&immediate, position_words, 1));
    CHECK(!load_normal_immediate(&immediate, normal_words, 28));
    CHECK(memcmp(&immediate, &indexed, sizeof(immediate)) == 0);
    return 1;
}

static int accepts_unresolved_zeroing_but_fails_closed_on_reference(void) {
    AcgcGxCanonicalTransformState state;

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    CHECK(load_position_indexed_unresolved(&state, 9));
    CHECK(load_normal_indexed_unresolved(&state, 9));
    CHECK((state.known_mask &
           ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(3)) == 0);
    CHECK((state.known_mask &
           ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(3)) == 0);
    CHECK(state.position[3][0] == 0);
    CHECK(state.normal[3][0] == 0);
    CHECK(acgc_gx_canonical_transform_state_validate(&state));

    /* The unresolved slot is non-renderable and cannot satisfy current use. */
    state.current_position_id = 9;
    CHECK(!acgc_gx_canonical_transform_state_validate(&state));
    CHECK(!load_position_indexed_unresolved(&state, 10));
    CHECK(!load_normal_indexed_unresolved(&state, 31));
    return 1;
}

static int accepts_deterministic_little_endian_bytes(void) {
    AcgcGxCanonicalTransformState first;
    AcgcGxCanonicalTransformState second;
    uint8_t first_bytes[ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE];
    uint8_t second_bytes[ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE];

    fill_state(&first, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    second = first;
    write_le_words(&first, first_bytes);
    write_le_words(&second, second_bytes);
    CHECK(memcmp(first_bytes, second_bytes, sizeof(first_bytes)) == 0);

    CHECK(first_bytes[0] == 0x00);
    CHECK(first_bytes[4] == 0x00);
    CHECK(first_bytes[5] == 0x00);
    CHECK(first_bytes[6] == 0x80);
    CHECK(first_bytes[7] == 0x3F);
    CHECK(first_bytes[ACGC_GX_CANONICAL_TRANSFORM_KNOWN_MASK_OFFSET + 0] ==
          0xFF);
    CHECK(first_bytes[ACGC_GX_CANONICAL_TRANSFORM_KNOWN_MASK_OFFSET + 1] ==
          0xFF);
    CHECK(first_bytes[ACGC_GX_CANONICAL_TRANSFORM_KNOWN_MASK_OFFSET + 2] ==
          0x3F);
    CHECK(first_bytes[ACGC_GX_CANONICAL_TRANSFORM_KNOWN_MASK_OFFSET + 3] ==
          0x00);
    CHECK(first_bytes[ACGC_GX_CANONICAL_TRANSFORM_POSITION_OFFSET] ==
          0x00);
    CHECK(first_bytes[ACGC_GX_CANONICAL_TRANSFORM_NORMAL_OFFSET] == 0x00);
    CHECK(first_bytes[ACGC_GX_CANONICAL_TRANSFORM_END_OFFSET - 1] != 0xA5);
    return 1;
}

static int encodes_exact_little_endian_field_order(void) {
    AcgcGxCanonicalTransformState state;
    uint8_t encoded[ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE];
    size_t position_offset;
    size_t normal_offset;
    uint32_t slot;
    uint32_t word;

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    CHECK(acgc_gx_canonical_transform_state_encode(
        &state, encoded, sizeof(encoded)));

    CHECK(read_le32(encoded + 0) == state.projection_type);
    for (word = 0;
         word < ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_WORD_COUNT;
         word++) {
        CHECK(read_le32(encoded + 0x004 + word * 4) ==
            state.projection[word]);
    }
    CHECK(read_le32(encoded + ACGC_GX_CANONICAL_TRANSFORM_KNOWN_MASK_OFFSET) ==
        state.known_mask);
    CHECK(read_le32(
        encoded + ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_ID_OFFSET) ==
        state.current_position_id);
    for (word = 0;
         word < ACGC_GX_CANONICAL_TRANSFORM_RESERVED_WORD_COUNT;
         word++) {
        CHECK(read_le32(encoded + 0x024 + word * 4) == state.reserved[word]);
    }

    position_offset = ACGC_GX_CANONICAL_TRANSFORM_POSITION_OFFSET;
    normal_offset = ACGC_GX_CANONICAL_TRANSFORM_NORMAL_OFFSET;
    for (slot = 0;
         slot < ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT;
         slot++) {
        for (word = 0;
             word < ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT;
             word++) {
            CHECK(read_le32(encoded + position_offset + word * 4) ==
                state.position[slot][word]);
        }
        position_offset +=
            ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT * 4;
    }
    for (slot = 0;
         slot < ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT;
         slot++) {
        for (word = 0;
             word < ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT;
             word++) {
            CHECK(read_le32(encoded + normal_offset + word * 4) ==
                state.normal[slot][word]);
        }
        normal_offset +=
            ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT * 4;
    }
    CHECK(position_offset == ACGC_GX_CANONICAL_TRANSFORM_NORMAL_OFFSET);
    CHECK(normal_offset == ACGC_GX_CANONICAL_TRANSFORM_END_OFFSET);
    return 1;
}

static int rejects_bad_encode_inputs_without_mutating_destination(void) {
    AcgcGxCanonicalTransformState state;
    uint8_t encoded[ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE];
    uint8_t before[ACGC_GX_CANONICAL_TRANSFORM_STATE_SIZE];

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    CHECK(!acgc_gx_canonical_transform_state_encode(
        NULL, encoded, sizeof(encoded)));
    CHECK(!acgc_gx_canonical_transform_state_encode(
        &state, NULL, sizeof(encoded)));

    memset(encoded, 0xA5, sizeof(encoded));
    memcpy(before, encoded, sizeof(before));
    CHECK(!acgc_gx_canonical_transform_state_encode(
        &state, encoded, sizeof(encoded) - 1));
    CHECK(memcmp(encoded, before, sizeof(encoded)) == 0);
    CHECK(!acgc_gx_canonical_transform_state_encode(
        &state, encoded, sizeof(encoded) + 1));
    CHECK(memcmp(encoded, before, sizeof(encoded)) == 0);

    state.reserved[0] = 1;
    CHECK(!acgc_gx_canonical_transform_state_encode(
        &state, encoded, sizeof(encoded)));
    CHECK(memcmp(encoded, before, sizeof(encoded)) == 0);

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    state.projection_type = 2;
    CHECK(!acgc_gx_canonical_transform_state_encode(
        &state, encoded, sizeof(encoded)));
    CHECK(memcmp(encoded, before, sizeof(encoded)) == 0);

    fill_state(&state, ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE);
    state.projection[0] = UINT32_C(0x7F800000);
    CHECK(!acgc_gx_canonical_transform_state_encode(
        &state, encoded, sizeof(encoded)));
    CHECK(memcmp(encoded, before, sizeof(encoded)) == 0);
    return 1;
}

static int rejects_non_exact_metadata(void) {
    AcgcGxCanonicalEnvelope envelope;
    AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    prepare_transform_envelope(&envelope);
    entry = transform_entry(&envelope);
    entry->section_version = 2;
    CHECK(!acgc_gx_canonical_transform_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_transform_envelope(&envelope);
    entry = transform_entry(&envelope);
    entry->byte_size = ACGC_GX_CANONICAL_TRANSFORM_SECTION_BYTE_SIZE - 4;
    envelope.header.payload_byte_size = entry->byte_size;
    envelope.header.total_byte_size =
        ACGC_GX_CANONICAL_ENVELOPE_PAYLOAD_OFFSET + entry->byte_size;
    CHECK(acgc_gx_canonical_envelope_validate(
        &envelope, envelope.header.total_byte_size));
    CHECK(!acgc_gx_canonical_transform_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_transform_envelope(&envelope);
    entry = transform_entry(&envelope);
    entry->count = 0;
    CHECK(!acgc_gx_canonical_transform_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_transform_envelope(&envelope);
    entry = transform_entry(&envelope);
    entry->capacity = 2;
    CHECK(!acgc_gx_canonical_transform_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    prepare_transform_envelope(&envelope);
    entry = transform_entry(&envelope);
    entry->valid_mask = 0;
    CHECK(!acgc_gx_canonical_transform_metadata_validate(
        &envelope, envelope.header.total_byte_size));

    CHECK(acgc_gx_canonical_envelope_init(&envelope));
    entry = transform_entry(&envelope);
    entry->section_version = 1;
    CHECK(!acgc_gx_canonical_transform_metadata_validate(
        &envelope, sizeof(envelope)));
    return 1;
}

int main(void) {
    if (!accepts_exact_layout_and_metadata() ||
        !accepts_every_logical_slot_and_current_relation() ||
        !rejects_bad_masks_ids_and_relations() ||
        !rejects_nonfinite_known_words() ||
        !accepts_unknown_zeroing_and_rejects_nonzero_unknowns() ||
        !accepts_immediate_and_resolved_indexed_equivalence() ||
        !accepts_unresolved_zeroing_but_fails_closed_on_reference() ||
        !accepts_deterministic_little_endian_bytes() ||
        !encodes_exact_little_endian_field_order() ||
        !rejects_bad_encode_inputs_without_mutating_destination() ||
        !rejects_non_exact_metadata()) {
        return 1;
    }
    printf("GX canonical Transform tests: PASS\n");
    return 0;
}
