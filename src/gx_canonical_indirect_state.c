#include "acgc/gx_canonical_indirect_state.h"

#include <string.h>

static int canonical_indirect_word_is_bounded(
    uint32_t value,
    uint32_t minimum,
    uint32_t maximum
) {
    return value >= minimum && value <= maximum;
}

static uint32_t canonical_indirect_active_mask(uint32_t active_count) {
    return active_count == 0 ? 0 :
        (UINT32_C(1) << active_count) - UINT32_C(1);
}

static int canonical_indirect_header_is_valid(
    const AcgcGxCanonicalIndirectHeader* header
) {
    return header != NULL &&
        header->version == ACGC_GX_CANONICAL_INDIRECT_STATE_VERSION &&
        header->section_id == ACGC_GX_CANONICAL_INDIRECT_SECTION_ID &&
        header->section_mask == ACGC_GX_CANONICAL_INDIRECT_SECTION_MASK &&
        header->byte_size == ACGC_GX_CANONICAL_INDIRECT_STATE_SIZE &&
        header->active_indirect_stage_count >=
            ACGC_GX_CANONICAL_INDIRECT_ACTIVE_STAGE_COUNT_MIN &&
        header->active_indirect_stage_count <=
            ACGC_GX_CANONICAL_INDIRECT_ACTIVE_STAGE_COUNT_MAX &&
        header->order_capacity ==
            ACGC_GX_CANONICAL_INDIRECT_ORDER_CAPACITY &&
        header->order_record_size ==
            ACGC_GX_CANONICAL_INDIRECT_ORDER_RECORD_SIZE &&
        header->order_offset == ACGC_GX_CANONICAL_INDIRECT_ORDER_OFFSET &&
        header->active_order_mask == canonical_indirect_active_mask(
            header->active_indirect_stage_count) &&
        header->matrix_capacity ==
            ACGC_GX_CANONICAL_INDIRECT_MATRIX_CAPACITY &&
        header->matrix_record_size ==
            ACGC_GX_CANONICAL_INDIRECT_MATRIX_RECORD_SIZE &&
        header->matrix_offset == ACGC_GX_CANONICAL_INDIRECT_MATRIX_OFFSET &&
        (header->matrix_valid_mask &
            ~ACGC_GX_CANONICAL_INDIRECT_MATRIX_VALID_MASK) == 0 &&
        header->reserved == 0;
}

static int canonical_indirect_order_is_valid(
    const AcgcGxCanonicalIndirectOrder* order
) {
    return order != NULL &&
        canonical_indirect_word_is_bounded(
            order->tex_coord,
            ACGC_GX_CANONICAL_INDIRECT_TEXCOORD_MIN,
            ACGC_GX_CANONICAL_INDIRECT_TEXCOORD_MAX) &&
        canonical_indirect_word_is_bounded(
            order->tex_map,
            ACGC_GX_CANONICAL_INDIRECT_TEXMAP_MIN,
            ACGC_GX_CANONICAL_INDIRECT_TEXMAP_MAX) &&
        canonical_indirect_word_is_bounded(
            order->scale_s,
            ACGC_GX_CANONICAL_INDIRECT_SCALE_MIN,
            ACGC_GX_CANONICAL_INDIRECT_SCALE_MAX) &&
        canonical_indirect_word_is_bounded(
            order->scale_t,
            ACGC_GX_CANONICAL_INDIRECT_SCALE_MIN,
            ACGC_GX_CANONICAL_INDIRECT_SCALE_MAX) &&
        order->reserved[0] == 0 && order->reserved[1] == 0;
}

static int canonical_indirect_matrix_is_valid(
    const AcgcGxCanonicalIndirectMatrix* matrix
) {
    return matrix != NULL &&
        matrix->s0 >= ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MIN &&
        matrix->s0 <= ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MAX &&
        matrix->t0 >= ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MIN &&
        matrix->t0 <= ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MAX &&
        matrix->s1 >= ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MIN &&
        matrix->s1 <= ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MAX &&
        matrix->t1 >= ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MIN &&
        matrix->t1 <= ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MAX &&
        matrix->s2 >= ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MIN &&
        matrix->s2 <= ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MAX &&
        matrix->t2 >= ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MIN &&
        matrix->t2 <= ACGC_GX_CANONICAL_INDIRECT_MATRIX_COEFFICIENT_MAX &&
        canonical_indirect_word_is_bounded(
            matrix->encoded_scale,
            ACGC_GX_CANONICAL_INDIRECT_ENCODED_SCALE_MIN,
            ACGC_GX_CANONICAL_INDIRECT_ENCODED_SCALE_MAX) &&
        matrix->reserved == 0;
}

int acgc_gx_canonical_indirect_state_validate(
    const AcgcGxCanonicalIndirectState* state
) {
    static const AcgcGxCanonicalIndirectOrder zero_order = {0};
    static const AcgcGxCanonicalIndirectMatrix zero_matrix = {0};
    uint32_t index;

    if (state == NULL || !canonical_indirect_header_is_valid(&state->header)) {
        return 0;
    }

    for (index = 0; index < ACGC_GX_CANONICAL_INDIRECT_ORDER_COUNT; index++) {
        if (index < state->header.active_indirect_stage_count) {
            if (!canonical_indirect_order_is_valid(&state->orders[index])) {
                return 0;
            }
        } else if (memcmp(&state->orders[index], &zero_order,
                          sizeof(zero_order)) != 0) {
            return 0;
        }
    }

    for (index = 0; index < ACGC_GX_CANONICAL_INDIRECT_MATRIX_COUNT; index++) {
        const uint32_t matrix_mask = UINT32_C(1) << index;

        if ((state->header.matrix_valid_mask & matrix_mask) != 0) {
            if (!canonical_indirect_matrix_is_valid(&state->matrices[index])) {
                return 0;
            }
        } else if (memcmp(&state->matrices[index], &zero_matrix,
                          sizeof(zero_matrix)) != 0) {
            return 0;
        }
    }
    return 1;
}

static int canonical_indirect_entry_is_absent(
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry
) {
    return entry != NULL &&
        entry->section_id == ACGC_GX_CANONICAL_INDIRECT_SECTION_ID &&
        entry->section_version == 0 &&
        entry->byte_offset == 0 &&
        entry->byte_size == 0 &&
        entry->count == 0 &&
        entry->capacity == 0 &&
        entry->valid_mask == 0 &&
        entry->reserved == 0;
}

int acgc_gx_canonical_indirect_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
) {
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    if (!acgc_gx_canonical_envelope_validate(
            envelope, envelope_byte_size)) {
        return 0;
    }

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_INDIRECT_SECTION_ID - 1];
    if ((envelope->header.present_state_mask &
            ACGC_GX_CANONICAL_INDIRECT_SECTION_MASK) == 0) {
        return canonical_indirect_entry_is_absent(entry);
    }

    return entry->section_id == ACGC_GX_CANONICAL_INDIRECT_SECTION_ID &&
        entry->section_version == ACGC_GX_CANONICAL_INDIRECT_SECTION_VERSION &&
        entry->byte_size == ACGC_GX_CANONICAL_INDIRECT_SECTION_BYTE_SIZE &&
        entry->count == ACGC_GX_CANONICAL_INDIRECT_SECTION_COUNT &&
        entry->capacity == ACGC_GX_CANONICAL_INDIRECT_SECTION_CAPACITY &&
        entry->valid_mask == ACGC_GX_CANONICAL_INDIRECT_SECTION_MASK &&
        entry->reserved == 0;
}

static int canonical_indirect_geometry_flag_is_valid(uint32_t value) {
    return value == 0 || value == 1;
}

static int canonical_indirect_position_matrix_id_is_exact(uint32_t id) {
    return id >= ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_FIRST &&
        id <= ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_LAST &&
        ((id - ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_FIRST) %
            ACGC_GX_CANONICAL_GEOMETRY_POSITION_MATRIX_ID_STRIDE) == 0;
}

static int canonical_indirect_geometry_context_is_valid(
    const AcgcGxCanonicalGeometryDependencyResults* dependencies
) {
    uint32_t index;

    if (dependencies == NULL ||
        !canonical_indirect_geometry_flag_is_valid(
            dependencies->transform_valid) ||
        !canonical_indirect_geometry_flag_is_valid(
            dependencies->texgens_valid) ||
        !canonical_indirect_geometry_flag_is_valid(
            dependencies->channels_valid) ||
        !canonical_indirect_geometry_flag_is_valid(
            dependencies->lighting_valid) ||
        !canonical_indirect_geometry_flag_is_valid(
            dependencies->bump_valid) ||
        dependencies->reserved0 != 0 ||
        (dependencies->required_geometry_present_mask &
            ~ACGC_GX_CANONICAL_GEOMETRY_VALID_ATTRIBUTE_MASK) != 0 ||
        dependencies->required_channel_mask > 3 ||
        (dependencies->required_lighting_mask & ~UINT32_C(0xFF)) != 0 ||
        (dependencies->required_bump_mask & ~UINT32_C(0xFF)) != 0 ||
        (dependencies->transform_position_known_mask & ~UINT32_C(0x3FF)) != 0 ||
        (dependencies->transform_normal_known_mask & ~UINT32_C(0x3FF)) != 0 ||
        !canonical_indirect_geometry_flag_is_valid(
            dependencies->transform_current_position_known) ||
        (dependencies->texgen_present_mask & ~UINT32_C(0xFF)) != 0 ||
        (dependencies->texgen_ordinary_known_mask & ~UINT32_C(0x7FF)) != 0 ||
        (dependencies->texgen_post_known_mask & ~UINT32_C(0x1FFFFF)) != 0 ||
        (dependencies->lighting_loaded_mask & ~UINT32_C(0xFF)) != 0 ||
        (dependencies->bump_known_mask & ~UINT32_C(0xFF)) != 0) {
        return 0;
    }

    for (index = 0; index < 8; index++) {
        if ((dependencies->texgen_present_mask & (UINT32_C(1) << index)) == 0 &&
            dependencies->texgen_selector[index] != 0) {
            return 0;
        }
    }
    for (index = 0; index < 4; index++) {
        if (dependencies->reserved[index] != 0) {
            return 0;
        }
    }
    if (dependencies->transform_current_position_known == 0 &&
        dependencies->transform_current_position_id != 0) {
        return 0;
    }
    if (dependencies->transform_current_position_known != 0 &&
        !canonical_indirect_position_matrix_id_is_exact(
            dependencies->transform_current_position_id)) {
        return 0;
    }
    return 1;
}

static int canonical_indirect_ordinary_texgen_selector_is_known(
    uint32_t selector,
    const AcgcGxCanonicalGeometryDependencyResults* dependencies
) {
    uint32_t record;

    if (selector < ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_FIRST ||
        selector > ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_LAST ||
        ((selector - ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_FIRST) %
            ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_STRIDE) != 0) {
        return 0;
    }
    record = (selector -
        ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_FIRST) /
        ACGC_GX_CANONICAL_GEOMETRY_ORDINARY_TEX_MATRIX_ID_STRIDE;
    return (dependencies->texgen_ordinary_known_mask &
        (UINT32_C(1) << record)) != 0;
}

static int canonical_indirect_tev_stage_is_direct(
    const AcgcGxCanonicalTevStage* stage
) {
    return stage != NULL &&
        stage->ind_stage == ACGC_GX_CANONICAL_TEV_INDIRECT_STAGE_MIN &&
        stage->ind_format == ACGC_GX_CANONICAL_TEV_INDIRECT_FORMAT_MIN &&
        stage->ind_bias == ACGC_GX_CANONICAL_TEV_INDIRECT_BIAS_MIN &&
        stage->ind_mtx == ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_OFF &&
        stage->ind_wrap_s == ACGC_GX_CANONICAL_TEV_INDIRECT_WRAP_MIN &&
        stage->ind_wrap_t == ACGC_GX_CANONICAL_TEV_INDIRECT_WRAP_MIN &&
        stage->ind_add_prev == 0 && stage->ind_lod == 0 &&
        stage->ind_alpha == ACGC_GX_CANONICAL_TEV_INDIRECT_ALPHA_MIN;
}

static int canonical_indirect_tev_matrix_slot(
    uint32_t matrix,
    uint32_t* slot
) {
    if (slot == NULL) {
        return 0;
    }
    switch (matrix) {
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_0:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S0:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T0:
            *slot = 0;
            return 1;
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_1:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S1:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T1:
            *slot = 1;
            return 1;
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_2:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_S2:
        case ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_T2:
            *slot = 2;
            return 1;
        default:
            return 0;
    }
}

int acgc_gx_canonical_indirect_state_validate_dependencies(
    const AcgcGxCanonicalIndirectState* state,
    const AcgcGxCanonicalTevState* tev,
    const AcgcGxCanonicalTextureState* texture,
    const AcgcGxCanonicalGeometryDependencyResults* geometry_dependencies
) {
    uint32_t direct_map_mask = 0;
    uint32_t indirect_map_mask = 0;
    uint32_t index;

    if (!acgc_gx_canonical_indirect_state_validate(state) ||
        !acgc_gx_canonical_tev_state_validate(tev)) {
        return 0;
    }
    if (texture != NULL &&
        !acgc_gx_canonical_texture_state_validate(texture)) {
        return 0;
    }
    if (geometry_dependencies != NULL &&
        !canonical_indirect_geometry_context_is_valid(
            geometry_dependencies)) {
        return 0;
    }

    for (index = 0; index < tev->header.active_stage_count; index++) {
        const AcgcGxCanonicalTevStage* stage = &tev->stages[index];
        uint32_t matrix_slot;

        if (stage->tex_map <= ACGC_GX_CANONICAL_TEV_TEXMAP_MAX) {
            direct_map_mask |= UINT32_C(1) << stage->tex_map;
        }
        /* GXSetTevDirect's frozen defaults are valid with zero Indirect stages. */
        if (!canonical_indirect_tev_stage_is_direct(stage)) {
            if (stage->ind_stage >=
                state->header.active_indirect_stage_count) {
                return 0;
            }
            if (stage->ind_mtx != ACGC_GX_CANONICAL_TEV_INDIRECT_MATRIX_OFF &&
                (!canonical_indirect_tev_matrix_slot(
                    stage->ind_mtx, &matrix_slot) ||
                 (state->header.matrix_valid_mask &
                    (UINT32_C(1) << matrix_slot)) == 0)) {
                return 0;
            }
        }
    }

    for (index = 0;
         index < state->header.active_indirect_stage_count;
         index++) {
        const AcgcGxCanonicalIndirectOrder* order = &state->orders[index];

        indirect_map_mask |= UINT32_C(1) << order->tex_map;
        if (texture != NULL &&
            (texture->header.known_map_mask &
                (UINT32_C(1) << order->tex_map)) == 0) {
            return 0;
        }
        if (geometry_dependencies != NULL &&
            (geometry_dependencies->texgen_present_mask &
                (UINT32_C(1) << order->tex_coord)) == 0) {
            return 0;
        }
        if (geometry_dependencies != NULL &&
            (!geometry_dependencies->texgens_valid ||
             !canonical_indirect_ordinary_texgen_selector_is_known(
                 geometry_dependencies->texgen_selector[order->tex_coord],
                 geometry_dependencies))) {
            return 0;
        }
    }

    return (direct_map_mask & indirect_map_mask) == 0;
}
