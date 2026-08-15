#include "pc_gx_internal.h"

#include <dolphin/gx/GXEnum.h>

#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t index;
    uint32_t target_mask;
} PCGXRawChannelTarget;

static void pc_gx_raw_channels_mark_invalid(void) {
    PCGXRawChannels* shadow = &g_gx.raw_channels;

    if (shadow->invalid != 0) return;
    memset(shadow, 0, sizeof(*shadow));
    shadow->invalid = 1;
}

static int pc_gx_raw_channels_control_target(
    uint32_t channel,
    PCGXRawChannelTarget* target
) {
    if (target == NULL) return 0;

    switch (channel) {
        case GX_COLOR0:
            target->index = 0;
            target->target_mask = PC_GX_RAW_CHANNEL_CONTROL_TARGET_COLOR;
            return 1;
        case GX_ALPHA0:
            target->index = 0;
            target->target_mask = PC_GX_RAW_CHANNEL_CONTROL_TARGET_ALPHA;
            return 1;
        case GX_COLOR0A0:
            target->index = 0;
            target->target_mask = PC_GX_RAW_CHANNEL_CONTROL_TARGET_BOTH;
            return 1;
        case GX_COLOR1:
            target->index = 1;
            target->target_mask = PC_GX_RAW_CHANNEL_CONTROL_TARGET_COLOR;
            return 1;
        case GX_ALPHA1:
            target->index = 1;
            target->target_mask = PC_GX_RAW_CHANNEL_CONTROL_TARGET_ALPHA;
            return 1;
        case GX_COLOR1A1:
            target->index = 1;
            target->target_mask = PC_GX_RAW_CHANNEL_CONTROL_TARGET_BOTH;
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_raw_channels_color_target(
    uint32_t channel,
    PCGXRawChannelTarget* target
) {
    if (target == NULL) return 0;

    switch (channel) {
        case GX_COLOR0:
            target->index = 0;
            target->target_mask = PC_GX_RAW_CHANNEL_COMPONENT_R |
                PC_GX_RAW_CHANNEL_COMPONENT_G |
                PC_GX_RAW_CHANNEL_COMPONENT_B;
            return 1;
        case GX_ALPHA0:
            target->index = 0;
            target->target_mask = PC_GX_RAW_CHANNEL_COMPONENT_A;
            return 1;
        case GX_COLOR0A0:
            target->index = 0;
            target->target_mask = PC_GX_RAW_CHANNEL_COMPONENT_ALL;
            return 1;
        case GX_COLOR1:
            target->index = 1;
            target->target_mask = PC_GX_RAW_CHANNEL_COMPONENT_R |
                PC_GX_RAW_CHANNEL_COMPONENT_G |
                PC_GX_RAW_CHANNEL_COMPONENT_B;
            return 1;
        case GX_ALPHA1:
            target->index = 1;
            target->target_mask = PC_GX_RAW_CHANNEL_COMPONENT_A;
            return 1;
        case GX_COLOR1A1:
            target->index = 1;
            target->target_mask = PC_GX_RAW_CHANNEL_COMPONENT_ALL;
            return 1;
        default:
            return 0;
    }
}

static int pc_gx_raw_channels_control_values_are_valid(
    uint32_t enable,
    uint32_t ambient_source,
    uint32_t material_source,
    uint32_t light_mask,
    uint32_t diffuse_function,
    uint32_t attenuation_function
) {
    if (enable > ACGC_GX_CANONICAL_CHANNEL_BOOLEAN_TRUE ||
        (ambient_source != ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG &&
         ambient_source != ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX) ||
        (material_source != ACGC_GX_CANONICAL_CHANNEL_SOURCE_REG &&
         material_source != ACGC_GX_CANONICAL_CHANNEL_SOURCE_VTX) ||
        light_mask > ACGC_GX_CANONICAL_CHANNEL_LIGHT_MASK_MAX ||
        diffuse_function > ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_CLAMP ||
        attenuation_function > ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_NONE) {
        return 0;
    }

    if (attenuation_function ==
            ACGC_GX_CANONICAL_CHANNEL_ATTENUATION_SPEC &&
        diffuse_function != ACGC_GX_CANONICAL_CHANNEL_DIFFUSE_NONE) {
        return 0;
    }
    return 1;
}

static PCGXRawChannelControl* pc_gx_raw_channels_control_at(
    PCGXRawChannelRecord* record,
    uint32_t control_index
) {
    return control_index == 0 ? &record->color : &record->alpha;
}

static void pc_gx_raw_channels_write_component(
    uint32_t* value,
    uint32_t* known_mask,
    uint32_t packed,
    uint32_t component_mask,
    uint32_t shift
) {
    if ((component_mask & (UINT32_C(1) << (shift / 8u))) == 0) return;

    *value = (*value & ~(UINT32_C(0xFF) << shift)) |
        (packed & (UINT32_C(0xFF) << shift));
    *known_mask |= UINT32_C(1) << (shift / 8u);
}

static void pc_gx_raw_channels_copy_control(
    AcgcGxCanonicalChannelControl* destination,
    const PCGXRawChannelControl* source
) {
    destination->enable = source->enable;
    destination->ambient_source = source->ambient_source;
    destination->material_source = source->material_source;
    destination->light_mask = source->light_mask;
    destination->diffuse_function = source->diffuse_function;
    destination->attenuation_function = source->attenuation_function;
}

void pc_gx_raw_channels_initialize(void) {
    memset(&g_gx.raw_channels, 0, sizeof(g_gx.raw_channels));
}

void pc_gx_raw_channels_set_num(uint32_t count) {
    PCGXRawChannels* shadow = &g_gx.raw_channels;

    if (shadow->invalid != 0) return;
    if (count > PC_GX_RAW_CHANNEL_RECORD_COUNT) {
        pc_gx_raw_channels_mark_invalid();
        return;
    }

    shadow->active_count = count;
    shadow->active_count_known = 1;
}

void pc_gx_raw_channels_set_control(
    uint32_t channel,
    uint32_t enable,
    uint32_t ambient_source,
    uint32_t material_source,
    uint32_t light_mask,
    uint32_t diffuse_function,
    uint32_t attenuation_function
) {
    PCGXRawChannels* shadow = &g_gx.raw_channels;
    PCGXRawChannelTarget target;
    uint32_t control_index;

    if (shadow->invalid != 0) return;
    if (!pc_gx_raw_channels_control_target(channel, &target) ||
        !pc_gx_raw_channels_control_values_are_valid(
            enable,
            ambient_source,
            material_source,
            light_mask,
            diffuse_function,
            attenuation_function
        )) {
        pc_gx_raw_channels_mark_invalid();
        return;
    }

    for (control_index = 0; control_index < 2; control_index++) {
        PCGXRawChannelControl* control;

        if ((target.target_mask & (UINT32_C(1) << control_index)) == 0)
            continue;
        control = pc_gx_raw_channels_control_at(
            &shadow->records[target.index],
            control_index
        );
        control->enable = enable;
        control->ambient_source = ambient_source;
        control->material_source = material_source;
        control->light_mask = light_mask;
        control->diffuse_function = diffuse_function;
        control->attenuation_function = attenuation_function;
        control->known_mask = PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ALL;
    }
}

void pc_gx_raw_channels_set_color(
    uint32_t channel,
    uint32_t color_packed,
    int material
) {
    PCGXRawChannels* shadow = &g_gx.raw_channels;
    PCGXRawChannelTarget target;
    PCGXRawChannelRecord* record;
    uint32_t* value;
    uint32_t* known_mask;

    if (shadow->invalid != 0) return;
    if ((material != 0 && material != 1) ||
        !pc_gx_raw_channels_color_target(channel, &target)) {
        pc_gx_raw_channels_mark_invalid();
        return;
    }

    record = &shadow->records[target.index];
    value = material != 0 ? &record->material_rgba8 : &record->ambient_rgba8;
    known_mask = material != 0 ?
        &record->material_known_mask : &record->ambient_known_mask;

    pc_gx_raw_channels_write_component(
        value, known_mask, color_packed, target.target_mask, 0
    );
    pc_gx_raw_channels_write_component(
        value, known_mask, color_packed, target.target_mask, 8
    );
    pc_gx_raw_channels_write_component(
        value, known_mask, color_packed, target.target_mask, 16
    );
    pc_gx_raw_channels_write_component(
        value, known_mask, color_packed, target.target_mask, 24
    );
}

int pc_gx_raw_channels_build_canonical(
    AcgcGxCanonicalChannelState* destination
) {
    const PCGXRawChannels* shadow = &g_gx.raw_channels;
    AcgcGxCanonicalChannelState candidate;
    uint32_t index;

    if (destination == NULL || shadow->invalid != 0 ||
        shadow->active_count_known != 1 ||
        shadow->active_count > ACGC_GX_CANONICAL_CHANNEL_STATE_ACTIVE_COUNT_MAX) {
        return 0;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.active_count = shadow->active_count;
    candidate.record_valid_mask = candidate.active_count == 0 ? 0 :
        (UINT32_C(1) << candidate.active_count) - UINT32_C(1);

    for (index = 0; index < candidate.active_count; index++) {
        const PCGXRawChannelRecord* source = &shadow->records[index];
        AcgcGxCanonicalChannelRecord* record = &candidate.records[index];

        if (source->color.known_mask !=
                PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ALL ||
            source->alpha.known_mask !=
                PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ALL ||
            source->ambient_known_mask !=
                PC_GX_RAW_CHANNEL_COMPONENT_ALL ||
            source->material_known_mask !=
                PC_GX_RAW_CHANNEL_COMPONENT_ALL) {
            return 0;
        }

        record->channel_index = index;
        pc_gx_raw_channels_copy_control(&record->color, &source->color);
        pc_gx_raw_channels_copy_control(&record->alpha, &source->alpha);
        record->ambient_rgba8 = source->ambient_rgba8;
        record->material_rgba8 = source->material_rgba8;
    }

    if (!acgc_gx_canonical_channel_state_validate(&candidate)) return 0;
    *destination = candidate;
    return 1;
}

const PCGXRawChannels* pc_gx_raw_channels_shadow_fixture(void) {
    return &g_gx.raw_channels;
}
