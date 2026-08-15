#include "pc_gx_internal.h"

#include <stdint.h>
#include <string.h>

/* This is the PC-side spelling of GXLightObj's 16 register words.  The raw
 * producer copies values from a caller object at the immediate-load boundary
 * and never retains the object pointer. */
typedef struct {
    uint32_t reserved[3];
    uint32_t color;
    f32 angular[3];
    f32 distance[3];
    f32 position[3];
    f32 direction[3];
} PCGXRawLightObject;

_Static_assert(
    sizeof(PCGXRawLightObject) == ACGC_GX_CANONICAL_LIGHTING_RECORD_SIZE,
    "PC GXLightObj register layout must remain 16 words"
);

static int pc_gx_raw_lighting_exact_slot(uint32_t light) {
    uint32_t slot;

    for (slot = 0; slot < PC_GX_RAW_LIGHTING_SLOT_COUNT; slot++) {
        if (light == (UINT32_C(1) << slot)) return (int)slot;
    }
    return -1;
}

static uint32_t pc_gx_raw_lighting_float_bits(f32 value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int pc_gx_raw_lighting_binary32_is_finite(uint32_t bits) {
    return (bits & UINT32_C(0x7F800000)) != UINT32_C(0x7F800000);
}

static uint8_t pc_gx_raw_lighting_invalid_groups(
    const PCGXRawLightObject* object
) {
    uint8_t invalid_mask = 0;
    uint32_t index;

    for (index = 0; index < 3; index++) {
        if (!pc_gx_raw_lighting_binary32_is_finite(
                pc_gx_raw_lighting_float_bits(object->angular[index]))) {
            invalid_mask |= (uint8_t)PC_GX_RAW_LIGHTING_KNOWN_ANGULAR;
        }
        if (!pc_gx_raw_lighting_binary32_is_finite(
                pc_gx_raw_lighting_float_bits(object->distance[index]))) {
            invalid_mask |= (uint8_t)PC_GX_RAW_LIGHTING_KNOWN_DISTANCE;
        }
        if (!pc_gx_raw_lighting_binary32_is_finite(
                pc_gx_raw_lighting_float_bits(object->position[index]))) {
            invalid_mask |= (uint8_t)PC_GX_RAW_LIGHTING_KNOWN_POSITION;
        }
        if (!pc_gx_raw_lighting_binary32_is_finite(
                pc_gx_raw_lighting_float_bits(object->direction[index]))) {
            invalid_mask |= (uint8_t)PC_GX_RAW_LIGHTING_KNOWN_DIRECTION;
        }
    }
    return invalid_mask;
}

static uint64_t pc_gx_raw_lighting_next_generation(void) {
    uint64_t generation = ++g_gx.raw_lighting.next_generation;

    if (generation == 0) {
        generation = ++g_gx.raw_lighting.next_generation;
    }
    return generation;
}

static void pc_gx_raw_lighting_mark_invalid(void) {
    /* Invalid input must not manufacture a slot-zero load or discard the
     * last value.  The sticky flag makes the next canonical publication fail
     * closed until pc_gx_init establishes a new known-empty epoch. */
    g_gx.raw_lighting.invalid = 1;
}

static uint32_t pc_gx_raw_lighting_logical_color(uint32_t color) {
    const uint8_t* bytes = (const uint8_t*)&color;

    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static void pc_gx_raw_lighting_copy_record(
    AcgcGxCanonicalLightingRecord* destination,
    const PCGXRawLightObject* source
) {
    uint32_t index;

    memset(destination, 0, sizeof(*destination));
    destination->color_rgba8 = pc_gx_raw_lighting_logical_color(source->color);
    for (index = 0; index < 3; index++) {
        destination->angular_attenuation[index] =
            pc_gx_raw_lighting_float_bits(source->angular[index]);
        destination->distance_attenuation[index] =
            pc_gx_raw_lighting_float_bits(source->distance[index]);
        destination->position[index] =
            pc_gx_raw_lighting_float_bits(source->position[index]);
        /* The object already contains the final GX register direction. */
        destination->direction[index] =
            pc_gx_raw_lighting_float_bits(source->direction[index]);
    }
}

static int pc_gx_raw_lighting_control_references_are_valid(
    const PCGXRawChannelControl* control,
    uint8_t loaded_mask,
    uint8_t unresolved_mask
) {
    if (control->known_mask != PC_GX_RAW_CHANNEL_CONTROL_KNOWN_ALL) {
        return 0;
    }
    if (control->enable == 0) return 1;
    return (control->light_mask &
            ((uint32_t)loaded_mask | (uint32_t)unresolved_mask)) ==
            control->light_mask &&
        (control->light_mask & (uint32_t)unresolved_mask) == 0;
}

static int pc_gx_raw_lighting_channel_references_are_valid(
    uint8_t loaded_mask,
    uint8_t unresolved_mask
) {
    const PCGXRawChannels* channels = &g_gx.raw_channels;
    uint32_t index;

    /* An empty known-light state is useful during reset before GXInit's
     * channel setters have run.  Once a light has been loaded, unknown or
     * malformed channel provenance cannot be treated as disabled. */
    if (channels->invalid != 0) return 0;
    if (loaded_mask == 0 && unresolved_mask == 0 &&
        channels->active_count_known == 0) {
        return 1;
    }
    if (channels->active_count_known == 0 ||
        channels->active_count > PC_GX_RAW_CHANNEL_RECORD_COUNT) {
        return 0;
    }

    for (index = 0; index < channels->active_count; index++) {
        const PCGXRawChannelRecord* record = &channels->records[index];

        if (!pc_gx_raw_lighting_control_references_are_valid(
                &record->color, loaded_mask, unresolved_mask) ||
            !pc_gx_raw_lighting_control_references_are_valid(
                &record->alpha, loaded_mask, unresolved_mask)) {
            return 0;
        }
    }
    return 1;
}

void pc_gx_raw_lighting_initialize(void) {
    memset(&g_gx.raw_lighting, 0, sizeof(g_gx.raw_lighting));
    g_gx.raw_lighting.known = 1;
}

void pc_gx_raw_lighting_load_immediate(void* light_object, uint32_t light) {
    PCGXRawLighting* shadow = &g_gx.raw_lighting;
    PCGXRawLightObject object;
    AcgcGxCanonicalLightingRecord record;
    uint8_t invalid_groups;
    uint64_t generation;
    int slot;

    if (shadow->invalid != 0) return;

    slot = pc_gx_raw_lighting_exact_slot(light);
    if (slot < 0 || light_object == NULL) {
        pc_gx_raw_lighting_mark_invalid();
        return;
    }

    /* The copy is local and synchronous: no caller pointer enters raw state. */
    memcpy(&object, light_object, sizeof(object));
    invalid_groups = pc_gx_raw_lighting_invalid_groups(&object);
    if (invalid_groups != 0) {
        shadow->slots[slot].invalid_mask |= invalid_groups;
        pc_gx_raw_lighting_mark_invalid();
        return;
    }

    pc_gx_raw_lighting_copy_record(&record, &object);
    generation = pc_gx_raw_lighting_next_generation();
    shadow->slots[slot].value = record;
    shadow->slots[slot].known_mask =
        (uint8_t)PC_GX_RAW_LIGHTING_KNOWN_ALL;
    shadow->slots[slot].invalid_mask = 0;
    shadow->slots[slot].generation = generation;
    shadow->loaded_mask |= (uint8_t)(UINT32_C(1) << slot);
    shadow->unresolved_indexed_mask &=
        (uint8_t)~(UINT32_C(1) << slot);
    shadow->known = 1;
}

void pc_gx_raw_lighting_load_indexed(uint32_t object_index, uint32_t light) {
    PCGXRawLighting* shadow = &g_gx.raw_lighting;
    uint64_t generation;
    int slot;

    (void)object_index;
    if (shadow->invalid != 0) return;

    slot = pc_gx_raw_lighting_exact_slot(light);
    if (slot < 0) {
        pc_gx_raw_lighting_mark_invalid();
        return;
    }

    generation = pc_gx_raw_lighting_next_generation();
    memset(&shadow->slots[slot].value, 0,
           sizeof(shadow->slots[slot].value));
    shadow->slots[slot].known_mask = 0;
    shadow->slots[slot].invalid_mask = 0;
    shadow->slots[slot].generation = generation;
    shadow->loaded_mask |= (uint8_t)(UINT32_C(1) << slot);
    shadow->unresolved_indexed_mask |=
        (uint8_t)(UINT32_C(1) << slot);
    shadow->known = 1;
}

int pc_gx_raw_lighting_build_canonical(
    AcgcGxCanonicalLightingState* destination
) {
    const PCGXRawLighting* shadow = &g_gx.raw_lighting;
    AcgcGxCanonicalLightingState candidate;
    uint32_t slot;

    if (destination == NULL || shadow->known == 0 || shadow->invalid != 0 ||
        (shadow->unresolved_indexed_mask &
         (uint8_t)~shadow->loaded_mask) != 0) {
        return 0;
    }

    memset(&candidate, 0, sizeof(candidate));
    for (slot = 0; slot < PC_GX_RAW_LIGHTING_SLOT_COUNT; slot++) {
        const PCGXRawLightingSlot* source = &shadow->slots[slot];
        const uint8_t slot_mask = (uint8_t)(UINT32_C(1) << slot);

        if ((shadow->loaded_mask & slot_mask) != 0) {
            if ((shadow->unresolved_indexed_mask & slot_mask) != 0 ||
                source->known_mask != PC_GX_RAW_LIGHTING_KNOWN_ALL ||
                source->invalid_mask != 0) {
                return 0;
            }
            candidate.loaded_mask |= UINT32_C(1) << slot;
            candidate.records[slot] = source->value;
        } else if (source->known_mask != 0 || source->invalid_mask != 0 ||
                   source->generation != 0 ||
                   memcmp(&source->value, &(AcgcGxCanonicalLightingRecord){0},
                          sizeof(source->value)) != 0) {
            return 0;
        }
    }

    if (!pc_gx_raw_lighting_channel_references_are_valid(
            shadow->loaded_mask, shadow->unresolved_indexed_mask) ||
        !acgc_gx_canonical_lighting_state_validate(&candidate)) {
        return 0;
    }

    *destination = candidate;
    return 1;
}

const PCGXRawLighting* pc_gx_raw_lighting_shadow_fixture(void) {
    return &g_gx.raw_lighting;
}
