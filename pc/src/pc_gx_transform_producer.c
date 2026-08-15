#include "pc_gx_transform_producer.h"

#include <dolphin/gx/GXEnum.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(
    PC_GX_TRANSFORM_POSITION_COUNT ==
        ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT,
    "raw and canonical Transform slot counts differ"
);
_Static_assert(
    PC_GX_TRANSFORM_POSITION_WORDS ==
        ACGC_GX_CANONICAL_TRANSFORM_POSITION_RECORD_WORD_COUNT,
    "raw and canonical Transform position sizes differ"
);
_Static_assert(
    PC_GX_TRANSFORM_NORMAL_WORDS ==
        ACGC_GX_CANONICAL_TRANSFORM_NORMAL_RECORD_WORD_COUNT,
    "raw and canonical Transform normal sizes differ"
);
_Static_assert(
    PC_GX_TRANSFORM_PROJECTION_WORDS ==
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_WORD_COUNT,
    "raw and canonical Transform projection sizes differ"
);

static int pc_gx_transform_words_are_zero(
    const uint32_t* words,
    size_t count
) {
    size_t index;

    for (index = 0; index < count; index++) {
        if (words[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int pc_gx_transform_words_are_finite(
    const uint32_t* words,
    size_t count
) {
    size_t index;

    for (index = 0; index < count; index++) {
        if ((words[index] & UINT32_C(0x7F800000)) ==
            UINT32_C(0x7F800000)) {
            return 0;
        }
    }
    return 1;
}

static int pc_gx_transform_reserved_is_zero(
    const uint8_t reserved[3]
) {
    return reserved[0] == 0 && reserved[1] == 0 && reserved[2] == 0;
}

static int pc_gx_transform_matrix_record_is_valid(
    const uint32_t* words,
    size_t word_count,
    uint8_t known,
    const uint8_t reserved[3]
) {
    if (known > 1 || !pc_gx_transform_reserved_is_zero(reserved)) {
        return 0;
    }
    if (known == 0) {
        return pc_gx_transform_words_are_zero(words, word_count);
    }
    return pc_gx_transform_words_are_finite(words, word_count);
}

static int pc_gx_transform_exact_slot(uint32_t id) {
    uint32_t slot;

    for (slot = 0;
         slot < ACGC_GX_CANONICAL_TRANSFORM_POSITION_SLOT_COUNT;
         slot++) {
        if (id == slot *
                ACGC_GX_CANONICAL_TRANSFORM_LOGICAL_POSITION_ID_STRIDE) {
            return (int)slot;
        }
    }
    return -1;
}

static int pc_gx_transform_projection_type_to_canonical(
    uint32_t raw_type,
    uint32_t* canonical_type
) {
    if (raw_type == (uint32_t)GX_PERSPECTIVE) {
        *canonical_type =
            ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_PERSPECTIVE;
        return 1;
    }
    if (raw_type == (uint32_t)GX_ORTHOGRAPHIC) {
        *canonical_type =
            ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_ORTHOGRAPHIC;
        return 1;
    }
    return 0;
}

static int pc_gx_transform_input_is_producible(
    const PCGXRawTransform* input,
    int* current_slot
) {
    uint32_t slot;
    uint32_t canonical_type;

    if (input == NULL || input->invalid != 0 ||
        !pc_gx_transform_reserved_is_zero(input->reserved) ||
        input->projection.known != 1 ||
        !pc_gx_transform_reserved_is_zero(input->projection.reserved) ||
        !pc_gx_transform_projection_type_to_canonical(
            input->projection.type,
            &canonical_type
        ) ||
        !pc_gx_transform_words_are_finite(
            input->projection.coefficients,
            PC_GX_TRANSFORM_PROJECTION_WORDS
        ) ||
        input->current_position_known != 1) {
        return 0;
    }

    for (slot = 0;
         slot < PC_GX_TRANSFORM_POSITION_COUNT;
         slot++) {
        if (input->position_indexed_unresolved[slot] > 1 ||
            input->normal_indexed_unresolved[slot] > 1 ||
            input->position_indexed_unresolved[slot] != 0 ||
            input->normal_indexed_unresolved[slot] != 0 ||
            !pc_gx_transform_matrix_record_is_valid(
                input->position[slot].words,
                PC_GX_TRANSFORM_POSITION_WORDS,
                input->position[slot].known,
                input->position[slot].reserved
            ) ||
            !pc_gx_transform_matrix_record_is_valid(
                input->normal[slot].words,
                PC_GX_TRANSFORM_NORMAL_WORDS,
                input->normal[slot].known,
                input->normal[slot].reserved
            )) {
            return 0;
        }
    }

    *current_slot = pc_gx_transform_exact_slot(
        input->current_position_id
    );
    return *current_slot >= 0 &&
        input->position[*current_slot].known == 1;
}

int pc_gx_raw_transform_build_canonical(
    const PCGXRawTransform* input,
    AcgcGxCanonicalTransformState* output
) {
    AcgcGxCanonicalTransformState candidate;
    uint32_t canonical_type;
    int current_slot;
    uint32_t slot;

    if (output == NULL || input == NULL ||
        !pc_gx_transform_input_is_producible(input, &current_slot) ||
        !pc_gx_transform_projection_type_to_canonical(
            input->projection.type,
            &canonical_type
        )) {
        return 0;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.projection_type = canonical_type;
    memcpy(
        candidate.projection,
        input->projection.coefficients,
        sizeof(candidate.projection)
    );
    candidate.known_mask =
        ACGC_GX_CANONICAL_TRANSFORM_PROJECTION_KNOWN_MASK;

    candidate.current_position_id = input->current_position_id;
    candidate.known_mask |=
        ACGC_GX_CANONICAL_TRANSFORM_CURRENT_POSITION_KNOWN_MASK;

    for (slot = 0;
         slot < PC_GX_TRANSFORM_POSITION_COUNT;
         slot++) {
        if (input->position[slot].known == 1) {
            memcpy(
                candidate.position[slot],
                input->position[slot].words,
                sizeof(candidate.position[slot])
            );
            candidate.known_mask |=
                ACGC_GX_CANONICAL_TRANSFORM_POSITION_KNOWN_MASK(slot);
        }
        if (input->normal[slot].known == 1) {
            memcpy(
                candidate.normal[slot],
                input->normal[slot].words,
                sizeof(candidate.normal[slot])
            );
            candidate.known_mask |=
                ACGC_GX_CANONICAL_TRANSFORM_NORMAL_KNOWN_MASK(slot);
        }
    }

    if (current_slot < 0 ||
        !acgc_gx_canonical_transform_state_validate(&candidate)) {
        return 0;
    }

    *output = candidate;
    return 1;
}
