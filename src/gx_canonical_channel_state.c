#include "acgc/gx_canonical_channel_state.h"

static int canonical_channel_boolean_is_valid(uint32_t value) {
    return value == ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_FALSE ||
        value == ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_TRUE;
}

static int canonical_channel_source_is_valid(uint32_t value) {
    return value == ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG ||
        value == ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX;
}

static int canonical_channel_diffuse_is_valid(uint32_t value) {
    return value == ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_NONE ||
        value == ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_SIGN ||
        value == ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_CLAMP;
}

static int canonical_channel_attenuation_is_valid(uint32_t value) {
    return value == ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_SPEC ||
        value == ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_SPOT ||
        value == ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_NONE;
}

static int canonical_channel_control_is_valid(
    const AcgcGxCanonicalChannelControl* control
) {
    if (control == NULL ||
        !canonical_channel_boolean_is_valid(control->enable) ||
        !canonical_channel_source_is_valid(control->ambient_source) ||
        !canonical_channel_source_is_valid(control->material_source) ||
        control->light_mask < ACGC_GX_CANONICAL_CHANNEL_LIGHT_MASK_MIN ||
        control->light_mask > ACGC_GX_CANONICAL_CHANNEL_LIGHT_MASK_MAX ||
        !canonical_channel_diffuse_is_valid(control->diffuse_function) ||
        !canonical_channel_attenuation_is_valid(
            control->attenuation_function)) {
        return 0;
    }

    /* GX_AF_SPEC has no diffuse contribution in the effective GX state. */
    if (control->attenuation_function ==
            ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_SPEC &&
        control->diffuse_function !=
            ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_NONE) {
        return 0;
    }
    return 1;
}

static int canonical_channel_control_is_zero(
    const AcgcGxCanonicalChannelControl* control
) {
    return control != NULL &&
        control->enable == 0 &&
        control->ambient_source == 0 &&
        control->material_source == 0 &&
        control->light_mask == 0 &&
        control->diffuse_function == 0 &&
        control->attenuation_function == 0;
}

static int canonical_channel_record_is_zero(
    const AcgcGxCanonicalChannelRecord* record
) {
    return record != NULL &&
        record->channel_index == 0 &&
        record->reserved == 0 &&
        canonical_channel_control_is_zero(&record->color) &&
        canonical_channel_control_is_zero(&record->alpha) &&
        record->ambient_rgba8 == 0 &&
        record->material_rgba8 == 0;
}

int acgc_gx_canonical_channel_state_validate(
    const AcgcGxCanonicalChannelState* state
) {
    uint32_t index;
    uint32_t expected_valid_mask;

    if (state == NULL ||
        state->active_count <
            ACGC_GX_CANONICAL_CHANNEL_STATE_ACTIVE_COUNT_MIN ||
        state->active_count >
            ACGC_GX_CANONICAL_CHANNEL_STATE_ACTIVE_COUNT_MAX ||
        state->record_valid_mask >
            ACGC_GX_CANONICAL_CHANNEL_VALID_MASK_MAX) {
        return 0;
    }

    expected_valid_mask = state->active_count == 0 ? 0 :
        (UINT32_C(1) << state->active_count) - UINT32_C(1);
    if (state->record_valid_mask != expected_valid_mask) {
        return 0;
    }

    for (index = 0;
         index < ACGC_GX_CANONICAL_CHANNEL_STATE_CAPACITY;
         index++) {
        const AcgcGxCanonicalChannelRecord* record = &state->records[index];

        if (index < state->active_count) {
            if (record->channel_index != index ||
                record->reserved != 0 ||
                !canonical_channel_control_is_valid(&record->color) ||
                !canonical_channel_control_is_valid(&record->alpha)) {
                return 0;
            }
        } else if (!canonical_channel_record_is_zero(record)) {
            return 0;
        }
    }
    return 1;
}

static int canonical_channel_entry_is_absent(
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry
) {
    return entry != NULL &&
        entry->section_id == ACGC_GX_CANONICAL_CHANNEL_SECTION_ID &&
        entry->section_version == 0 &&
        entry->byte_offset == 0 &&
        entry->byte_size == 0 &&
        entry->count == 0 &&
        entry->capacity == 0 &&
        entry->valid_mask == 0 &&
        entry->reserved == 0;
}

int acgc_gx_canonical_channel_metadata_validate(
    const AcgcGxCanonicalEnvelope* envelope,
    size_t envelope_byte_size
) {
    const AcgcGxCanonicalEnvelopeDirectoryEntry* entry;

    if (!acgc_gx_canonical_envelope_validate(
            envelope, envelope_byte_size)) {
        return 0;
    }

    entry = &envelope->directory[
        ACGC_GX_CANONICAL_CHANNEL_SECTION_ID - 1];
    if ((envelope->header.present_state_mask &
            ACGC_GX_CANONICAL_CHANNEL_SECTION_MASK) == 0) {
        return canonical_channel_entry_is_absent(entry);
    }

    return entry->section_id == ACGC_GX_CANONICAL_CHANNEL_SECTION_ID &&
        entry->section_version == ACGC_GX_CANONICAL_CHANNEL_SECTION_VERSION &&
        entry->byte_size == ACGC_GX_CANONICAL_CHANNEL_SECTION_BYTE_SIZE &&
        entry->count == ACGC_GX_CANONICAL_CHANNEL_SECTION_COUNT &&
        entry->capacity == ACGC_GX_CANONICAL_CHANNEL_SECTION_CAPACITY &&
        entry->valid_mask == ACGC_GX_CANONICAL_CHANNEL_SECTION_MASK &&
        entry->reserved == 0;
}
