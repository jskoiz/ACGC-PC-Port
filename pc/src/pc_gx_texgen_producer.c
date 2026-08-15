#include "pc_gx_texgen_producer.h"

#include <dolphin/gx/GXEnum.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(
    GX_TG_MTX3x4 == ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX3X4,
    "GX and canonical MTX3x4 selectors differ"
);
_Static_assert(
    GX_TG_MTX2x4 == ACGC_GX_CANONICAL_TEXGEN_FUNCTION_MTX2X4,
    "GX and canonical MTX2x4 selectors differ"
);
_Static_assert(
    GX_TG_BUMP0 == ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP0 &&
        GX_TG_BUMP7 == ACGC_GX_CANONICAL_TEXGEN_FUNCTION_BUMP7,
    "GX and canonical bump selectors differ"
);
_Static_assert(
    GX_TG_SRTG == ACGC_GX_CANONICAL_TEXGEN_FUNCTION_SRTG,
    "GX and canonical SRTG selectors differ"
);
_Static_assert(
    GX_TG_POS == ACGC_GX_CANONICAL_TEXGEN_SOURCE_POS &&
        GX_TG_TEX7 == ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEX7 &&
        GX_TG_TEXCOORD0 == ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD0 &&
        GX_TG_TEXCOORD6 == ACGC_GX_CANONICAL_TEXGEN_SOURCE_TEXCOORD6 &&
        GX_TG_COLOR1 == ACGC_GX_CANONICAL_TEXGEN_SOURCE_COLOR1,
    "GX and canonical Texgen source selectors differ"
);
_Static_assert(
    GX_MTX3x4 == ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX3X4 &&
        GX_MTX2x4 == ACGC_GX_CANONICAL_TEXGEN_MATRIX_LOAD_MTX2X4,
    "GX and canonical matrix load selectors differ"
);
_Static_assert(
    GX_TEXMTX0 == ACGC_GX_CANONICAL_TEXGEN_ORDINARY_LOGICAL_ID(0) &&
        GX_IDENTITY == ACGC_GX_CANONICAL_TEXGEN_ORDINARY_LOGICAL_ID(10) &&
        GX_PTTEXMTX0 == ACGC_GX_CANONICAL_TEXGEN_POST_LOGICAL_ID(0) &&
        GX_PTIDENTITY == ACGC_GX_CANONICAL_TEXGEN_POST_LOGICAL_ID(20),
    "GX and canonical matrix logical IDs differ"
);

static int pc_gx_texgen_words_are_zero(
    const uint32_t* words,
    size_t count
) {
    size_t index;

    for (index = 0; index < count; index++) {
        if (words[index] != 0) return 0;
    }
    return 1;
}

static int pc_gx_texgen_words_are_finite(
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

static uint32_t pc_gx_texgen_popcount(uint32_t value) {
    uint32_t count = 0;

    while (value != 0) {
        count += value & 1u;
        value >>= 1;
    }
    return count;
}

static int pc_gx_texgen_bool_is_valid(uint32_t value) {
    return value == (uint32_t)GX_FALSE || value == (uint32_t)GX_TRUE;
}

static int pc_gx_texgen_function_is_regular(uint32_t function) {
    return function == (uint32_t)GX_TG_MTX2x4 ||
        function == (uint32_t)GX_TG_MTX3x4;
}

static int pc_gx_texgen_regular_source_is_valid(uint32_t source) {
    return source <= (uint32_t)GX_TG_TANGENT ||
        (source >= (uint32_t)GX_TG_TEX0 &&
         source <= (uint32_t)GX_TG_TEX7) ||
        source == (uint32_t)GX_TG_COLOR0 ||
        source == (uint32_t)GX_TG_COLOR1;
}

static int pc_gx_texgen_record_values_are_valid(
    uint32_t function,
    uint32_t source
) {
    if (pc_gx_texgen_function_is_regular(function)) {
        return pc_gx_texgen_regular_source_is_valid(source);
    }
    if (function >= (uint32_t)GX_TG_BUMP0 &&
        function <= (uint32_t)GX_TG_BUMP7) {
        return source >= (uint32_t)GX_TG_TEXCOORD0 &&
            source <= (uint32_t)GX_TG_TEXCOORD6;
    }
    return function == (uint32_t)GX_TG_SRTG &&
        (source == (uint32_t)GX_TG_COLOR0 ||
         source == (uint32_t)GX_TG_COLOR1);
}

static int pc_gx_texgen_ordinary_slot(uint32_t id) {
    if (id == (uint32_t)GX_IDENTITY) {
        return PC_GX_TEXGEN_ORDINARY_MATRIX_COUNT - 1;
    }
    if (id >= (uint32_t)GX_TEXMTX0 && id <= (uint32_t)GX_TEXMTX9 &&
        ((id - (uint32_t)GX_TEXMTX0) % 3u) == 0u) {
        return (int)((id - (uint32_t)GX_TEXMTX0) / 3u);
    }
    return -1;
}

static int pc_gx_texgen_post_slot(uint32_t id) {
    if (id == (uint32_t)GX_PTIDENTITY) {
        return PC_GX_TEXGEN_POST_MATRIX_COUNT - 1;
    }
    if (id >= (uint32_t)GX_PTTEXMTX0 &&
        id <= (uint32_t)GX_PTTEXMTX19 &&
        ((id - (uint32_t)GX_PTTEXMTX0) % 3u) == 0u) {
        return (int)((id - (uint32_t)GX_PTTEXMTX0) / 3u);
    }
    return -1;
}

static int pc_gx_texgen_matrix_type_is_valid(
    int post,
    uint32_t type
) {
    if (post) return type == (uint32_t)GX_MTX3x4;
    return type == (uint32_t)GX_MTX2x4 ||
        type == (uint32_t)GX_MTX3x4;
}

static uint32_t pc_gx_texgen_matrix_word_count(uint32_t type) {
    return type == (uint32_t)GX_MTX2x4 ? 8u : 12u;
}

static uint32_t pc_gx_texgen_matrix_word_mask(uint32_t count) {
    return count == 8u ?
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_2X4 :
        ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4;
}

static int pc_gx_texgen_record_is_complete(
    const PCGXRawTexgenRecord* record
) {
    return record != NULL &&
        record->component_known == PC_GX_TEXGEN_KNOWN_ALL &&
        pc_gx_texgen_record_values_are_valid(
            record->function,
            record->source
        ) &&
        pc_gx_texgen_ordinary_slot(record->ordinary_matrix_id) >= 0 &&
        pc_gx_texgen_post_slot(record->post_matrix_id) >= 0 &&
        pc_gx_texgen_bool_is_valid(record->normalize);
}

static int pc_gx_texgen_matrix_record_is_valid(
    const PCGXRawTexMatrix* record,
    uint32_t expected_id,
    int post
) {
    uint32_t word;

    if (record == NULL || record->logical_id != expected_id ||
        (record->slot_known != 0 && record->slot_known != 1) ||
        (record->known_word_mask &
            ~ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4) != 0) {
        return 0;
    }
    for (word = 0;
         word < PC_GX_TEXGEN_MATRIX_WORD_COUNT;
         word++) {
        if ((record->known_word_mask & (UINT32_C(1) << word)) == 0 &&
            record->words[word] != 0) {
            return 0;
        }
        if ((record->known_word_mask & (UINT32_C(1) << word)) != 0 &&
            !pc_gx_texgen_words_are_finite(&record->words[word], 1)) {
            return 0;
        }
    }
    if (record->slot_known == 0) {
        return record->provenance ==
                PC_GX_TEXGEN_MATRIX_PROVENANCE_NONE &&
            record->last_load_type == 0 &&
            record->last_written_word_count == 0 &&
            record->known_word_mask == 0 &&
            pc_gx_texgen_words_are_zero(
                record->words,
                PC_GX_TEXGEN_MATRIX_WORD_COUNT
            );
    }

    /* A setter-created known slot always represents an attempted 2x4 or 3x4
     * load.  Empty known slots and INVALID provenance only arise from a
     * rejected setter call, which also makes the raw snapshot sticky-invalid.
     */
    if (record->provenance != PC_GX_TEXGEN_MATRIX_PROVENANCE_IMMEDIATE &&
        record->provenance !=
            PC_GX_TEXGEN_MATRIX_PROVENANCE_INDEXED_UNRESOLVED) {
        return 0;
    }
    if (record->last_written_word_count != 8u &&
        record->last_written_word_count != 12u) {
        return 0;
    }
    if (post && record->last_written_word_count != 12u) return 0;
    if (!pc_gx_texgen_matrix_type_is_valid(post, record->last_load_type)) {
        return 0;
    }
    if (record->last_written_word_count !=
        pc_gx_texgen_matrix_word_count(record->last_load_type)) {
        return 0;
    }
    if (record->provenance ==
            PC_GX_TEXGEN_MATRIX_PROVENANCE_INDEXED_UNRESOLVED &&
        (record->known_word_mask & pc_gx_texgen_matrix_word_mask(
            record->last_written_word_count)) ==
            pc_gx_texgen_matrix_word_mask(record->last_written_word_count)) {
        return 0;
    }
    return 1;
}

static int pc_gx_texgen_matrix_range_is_known(
    const PCGXRawTexMatrix* record,
    uint32_t word_count
) {
    return record != NULL && record->slot_known == 1 &&
        (record->known_word_mask &
            pc_gx_texgen_matrix_word_mask(word_count)) ==
            pc_gx_texgen_matrix_word_mask(word_count);
}

static int pc_gx_texgen_su_record_is_valid(
    const PCGXRawTexcoordSU* record
) {
    uint32_t known;

    if (record == NULL) return 0;
    known = record->component_known;
    if ((known & ~UINT32_C(0x0000007F)) != 0) return 0;
    if ((known & PC_GX_TEXGEN_SU_KNOWN_MANUAL) == 0 &&
        record->manual_enable != 0) return 0;
    if ((known & PC_GX_TEXGEN_SU_KNOWN_SCALE_S) == 0 &&
        record->scale_s_raw_u16 != 0) return 0;
    if ((known & PC_GX_TEXGEN_SU_KNOWN_SCALE_T) == 0 &&
        record->scale_t_raw_u16 != 0) return 0;
    if ((known & PC_GX_TEXGEN_SU_KNOWN_BIAS_S) == 0 &&
        record->bias_s != 0) return 0;
    if ((known & PC_GX_TEXGEN_SU_KNOWN_BIAS_T) == 0 &&
        record->bias_t != 0) return 0;
    if ((known & PC_GX_TEXGEN_SU_KNOWN_CYLINDER_S) == 0 &&
        record->cylinder_s != 0) return 0;
    if ((known & PC_GX_TEXGEN_SU_KNOWN_CYLINDER_T) == 0 &&
        record->cylinder_t != 0) return 0;
    return ((known & PC_GX_TEXGEN_SU_KNOWN_MANUAL) == 0 ||
            pc_gx_texgen_bool_is_valid(record->manual_enable)) &&
        ((known & PC_GX_TEXGEN_SU_KNOWN_BIAS_S) == 0 ||
         pc_gx_texgen_bool_is_valid(record->bias_s)) &&
        ((known & PC_GX_TEXGEN_SU_KNOWN_BIAS_T) == 0 ||
         pc_gx_texgen_bool_is_valid(record->bias_t)) &&
        ((known & PC_GX_TEXGEN_SU_KNOWN_CYLINDER_S) == 0 ||
         pc_gx_texgen_bool_is_valid(record->cylinder_s)) &&
        ((known & PC_GX_TEXGEN_SU_KNOWN_CYLINDER_T) == 0 ||
         pc_gx_texgen_bool_is_valid(record->cylinder_t));
}

static int pc_gx_texgen_input_is_valid(
    const PCGXRawTexgen* input
) {
    uint32_t bump_count = 0;
    uint32_t color_count = 0;
    uint32_t phase = 0;
    uint32_t color_mask = 0;
    uint32_t index;

    if (input == NULL || input->invalid != 0 ||
        input->active_texgen_count_known != 1 ||
        input->active_texgen_count > PC_GX_TEXGEN_COUNT) {
        return 0;
    }
    for (index = 0; index < PC_GX_TEXGEN_COUNT; index++) {
        const PCGXRawTexgenRecord* record = &input->texgen[index];

        if ((record->component_known & ~PC_GX_TEXGEN_KNOWN_ALL) != 0 ||
            (record->component_known != 0 &&
             record->component_known != PC_GX_TEXGEN_KNOWN_ALL)) {
            return 0;
        }
        if (record->component_known == 0 &&
            (record->function != 0 || record->source != 0 ||
             record->ordinary_matrix_id != 0 || record->normalize != 0 ||
             record->post_matrix_id != 0)) {
            return 0;
        }
        if (record->component_known == PC_GX_TEXGEN_KNOWN_ALL &&
            !pc_gx_texgen_record_is_complete(record)) {
            return 0;
        }
        if (!pc_gx_texgen_su_record_is_valid(&input->su[index])) {
            return 0;
        }
    }
    for (index = 0; index < PC_GX_TEXGEN_ORDINARY_MATRIX_COUNT; index++) {
        uint32_t expected_id = index < 10u ?
            (uint32_t)(GX_TEXMTX0 + index * 3u) :
            (uint32_t)GX_IDENTITY;

        if (!pc_gx_texgen_matrix_record_is_valid(
                &input->ordinary[index], expected_id, 0)) {
            return 0;
        }
    }
    for (index = 0; index < PC_GX_TEXGEN_POST_MATRIX_COUNT; index++) {
        uint32_t expected_id = index < 20u ?
            (uint32_t)(GX_PTTEXMTX0 + index * 3u) :
            (uint32_t)GX_PTIDENTITY;

        if (!pc_gx_texgen_matrix_record_is_valid(
                &input->post[index], expected_id, 1)) {
            return 0;
        }
    }

    for (index = 0; index < input->active_texgen_count; index++) {
        const PCGXRawTexgenRecord* record = &input->texgen[index];
        const PCGXRawTexMatrix* ordinary;
        const PCGXRawTexMatrix* post;
        uint32_t ordinary_word_count = 0;
        int ordinary_slot;
        int post_slot;

        if (!pc_gx_texgen_record_is_complete(record)) return 0;
        if (pc_gx_texgen_function_is_regular(record->function)) {
            if (phase != 0) return 0;
            ordinary_word_count = record->function ==
                (uint32_t)GX_TG_MTX2x4 ? 8u : 12u;
        } else if (record->function >= (uint32_t)GX_TG_BUMP0 &&
                   record->function <= (uint32_t)GX_TG_BUMP7) {
            uint32_t source_index;

            if (phase > 1 || ++bump_count > 3) return 0;
            source_index = record->source - (uint32_t)GX_TG_TEXCOORD0;
            if (source_index >= index ||
                input->texgen[source_index].component_known !=
                    PC_GX_TEXGEN_KNOWN_ALL ||
                !pc_gx_texgen_function_is_regular(
                    input->texgen[source_index].function)) {
                return 0;
            }
            phase = 1;
        } else {
            if (phase > 2 || ++color_count > 2) return 0;
            phase = 2;
            if (record->source == (uint32_t)GX_TG_COLOR0) {
                if (color_count != 1 || (color_mask & 1u) != 0) return 0;
                color_mask |= 1u;
            } else {
                if (color_count == 1 || (color_mask & 2u) != 0) return 0;
                color_mask |= 2u;
            }
        }

        ordinary_slot = pc_gx_texgen_ordinary_slot(
            record->ordinary_matrix_id
        );
        post_slot = pc_gx_texgen_post_slot(record->post_matrix_id);
        if (ordinary_slot < 0 || post_slot < 0) return 0;
        ordinary = &input->ordinary[ordinary_slot];
        post = &input->post[post_slot];
        if ((ordinary_word_count != 0 &&
             !pc_gx_texgen_matrix_range_is_known(
                 ordinary, ordinary_word_count
             )) || !pc_gx_texgen_matrix_range_is_known(post, 12u)) {
            return 0;
        }
    }
    return 1;
}

static void pc_gx_texgen_set_summary(
    AcgcGxCanonicalTexgenState* state
) {
    uint32_t index;
    int all_texgen = 1;
    int all_ordinary = 1;
    int all_post = 1;
    int all_su = 1;

    for (index = 0; index < ACGC_GX_CANONICAL_TEXGEN_COUNT; index++) {
        if (state->texgen[index].component_known !=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL) {
            all_texgen = 0;
        }
        if (state->su[index].component_known !=
            ACGC_GX_CANONICAL_TEXGEN_SU_COMPONENT_ALL) {
            all_su = 0;
        }
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_COUNT;
         index++) {
        if (state->ordinary_matrix[index].known_word_mask !=
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4) {
            all_ordinary = 0;
        }
    }
    for (index = 0;
         index < ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_COUNT;
         index++) {
        if (state->post_matrix[index].known_word_mask !=
            ACGC_GX_CANONICAL_TEXGEN_MATRIX_WORD_MASK_3X4) {
            all_post = 0;
        }
    }

    state->header.component_known_summary = 0;
    if (all_texgen) {
        state->header.component_known_summary |=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_TEXGEN;
    }
    if (all_ordinary) {
        state->header.component_known_summary |=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_ORDINARY_MATRIX;
    }
    if (all_post) {
        state->header.component_known_summary |=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_POST_MATRIX;
    }
    if (all_su) {
        state->header.component_known_summary |=
            ACGC_GX_CANONICAL_TEXGEN_COMPONENT_SUMMARY_SU;
    }
}

int pc_gx_raw_texgen_build_canonical(
    const PCGXRawTexgen* input,
    AcgcGxCanonicalTexgenState* output
) {
    AcgcGxCanonicalTexgenState candidate;
    uint32_t index;

    if (output == NULL || !pc_gx_texgen_input_is_valid(input)) return 0;

    memset(&candidate, 0, sizeof(candidate));
    candidate.header.active_texgen_count = input->active_texgen_count;
    candidate.header.texgen_capacity =
        ACGC_GX_CANONICAL_TEXGEN_CAPACITY;
    candidate.header.ordinary_matrix_capacity =
        ACGC_GX_CANONICAL_TEXGEN_ORDINARY_MATRIX_CAPACITY;
    candidate.header.post_matrix_capacity =
        ACGC_GX_CANONICAL_TEXGEN_POST_MATRIX_CAPACITY;
    candidate.header.su_capacity = ACGC_GX_CANONICAL_TEXGEN_SU_CAPACITY;

    for (index = 0; index < PC_GX_TEXGEN_COUNT; index++) {
        const PCGXRawTexgenRecord* source = &input->texgen[index];
        AcgcGxCanonicalTexgenRecord* destination = &candidate.texgen[index];
        AcgcGxCanonicalTexgenSuRecord* su_destination = &candidate.su[index];
        const PCGXRawTexcoordSU* su_source = &input->su[index];

        if (source->component_known == PC_GX_TEXGEN_KNOWN_ALL) {
            destination->function = source->function;
            destination->source = source->source;
            destination->ordinary_matrix_id = source->ordinary_matrix_id;
            destination->normalize = source->normalize;
            destination->post_matrix_id = source->post_matrix_id;
            destination->component_known =
                ACGC_GX_CANONICAL_TEXGEN_COMPONENT_ALL;
            candidate.header.texgen_known_mask |= UINT32_C(1) << index;
        }

        su_destination->component_known = su_source->component_known;
        su_destination->manual_enable = su_source->manual_enable;
        su_destination->scale_s_raw_u16 = su_source->scale_s_raw_u16;
        su_destination->scale_t_raw_u16 = su_source->scale_t_raw_u16;
        su_destination->bias_s = su_source->bias_s;
        su_destination->bias_t = su_source->bias_t;
        su_destination->cylinder_s = su_source->cylinder_s;
        su_destination->cylinder_t = su_source->cylinder_t;
        if (su_source->component_known != 0) {
            candidate.header.su_known_mask |= UINT32_C(1) << index;
        }
    }

    for (index = 0;
         index < PC_GX_TEXGEN_ORDINARY_MATRIX_COUNT;
         index++) {
        const PCGXRawTexMatrix* source = &input->ordinary[index];
        AcgcGxCanonicalTexgenMatrixRecord* destination =
            &candidate.ordinary_matrix[index];

        destination->logical_id = source->logical_id;
        if (source->slot_known == 1) {
            destination->last_load_type = source->last_load_type;
            destination->last_written_word_count =
                source->last_written_word_count;
            destination->known_word_mask = source->known_word_mask;
            memcpy(destination->words, source->words,
                   sizeof(destination->words));
            candidate.header.ordinary_matrix_known_mask |=
                UINT32_C(1) << index;
        }
    }
    for (index = 0;
         index < PC_GX_TEXGEN_POST_MATRIX_COUNT;
         index++) {
        const PCGXRawTexMatrix* source = &input->post[index];
        AcgcGxCanonicalTexgenMatrixRecord* destination =
            &candidate.post_matrix[index];

        destination->logical_id = source->logical_id;
        if (source->slot_known == 1) {
            destination->last_load_type = source->last_load_type;
            destination->last_written_word_count =
                source->last_written_word_count;
            destination->known_word_mask = source->known_word_mask;
            memcpy(destination->words, source->words,
                   sizeof(destination->words));
            candidate.header.post_matrix_known_mask |= UINT32_C(1) << index;
        }
    }

    candidate.header.known_texgen_count = pc_gx_texgen_popcount(
        candidate.header.texgen_known_mask
    );
    candidate.header.ordinary_matrix_count = pc_gx_texgen_popcount(
        candidate.header.ordinary_matrix_known_mask
    );
    candidate.header.post_matrix_count = pc_gx_texgen_popcount(
        candidate.header.post_matrix_known_mask
    );
    candidate.header.su_count = pc_gx_texgen_popcount(
        candidate.header.su_known_mask
    );
    pc_gx_texgen_set_summary(&candidate);

    if (!acgc_gx_canonical_texgen_state_validate(&candidate)) return 0;

    *output = candidate;
    return 1;
}
