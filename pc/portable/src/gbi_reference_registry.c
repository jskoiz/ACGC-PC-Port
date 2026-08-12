#include "acgc/gbi_reference_registry.h"

#include <string.h>

_Static_assert(
    ACGC_GBI_REFERENCE_REGISTRY_CAPACITY <=
        (ACGC_GBI_REFERENCE_HANDLE_SLOT_MASK + UINT32_C(1)),
    "GBI reference registry capacity does not fit its handle slot field"
);

static uint16_t next_generation(uint16_t generation) {
    uint32_t next = ((uint32_t)generation & ACGC_GBI_REFERENCE_HANDLE_GENERATION_MASK) + UINT32_C(1);

    if (next > ACGC_GBI_REFERENCE_HANDLE_GENERATION_MASK) {
        next = UINT32_C(1);
    }
    return (uint16_t)next;
}

static uint32_t make_handle(uint32_t slot, uint16_t generation) {
    return ACGC_GBI_REFERENCE_HANDLE_PREFIX |
           (((uint32_t)generation & ACGC_GBI_REFERENCE_HANDLE_GENERATION_MASK)
            << ACGC_GBI_REFERENCE_HANDLE_GENERATION_SHIFT) |
           (slot & ACGC_GBI_REFERENCE_HANDLE_SLOT_MASK);
}

static AcgcGbiReferenceStatus decode_handle(
    uint32_t handle,
    uint32_t* out_slot,
    uint16_t* out_generation
) {
    uint32_t generation;

    if ((handle & ACGC_GBI_REFERENCE_HANDLE_PREFIX_MASK) !=
        ACGC_GBI_REFERENCE_HANDLE_PREFIX) {
        return ACGC_GBI_REFERENCE_INVALID_HANDLE;
    }

    generation = (handle >> ACGC_GBI_REFERENCE_HANDLE_GENERATION_SHIFT) &
                 ACGC_GBI_REFERENCE_HANDLE_GENERATION_MASK;
    if (generation == 0) {
        return ACGC_GBI_REFERENCE_INVALID_HANDLE;
    }

    *out_slot = handle & ACGC_GBI_REFERENCE_HANDLE_SLOT_MASK;
    *out_generation = (uint16_t)generation;
    return ACGC_GBI_REFERENCE_OK;
}

void acgc_gbi_reference_registry_init(AcgcGbiReferenceRegistry* registry) {
    uint32_t slot;

    if (registry == NULL) {
        return;
    }

    memset(registry, 0, sizeof(*registry));
    for (slot = 0; slot < ACGC_GBI_REFERENCE_REGISTRY_CAPACITY; slot++) {
        registry->entries[slot].generation = UINT16_C(1);
    }
}

void acgc_gbi_reference_registry_reset(AcgcGbiReferenceRegistry* registry) {
    uint32_t slot;

    if (registry == NULL) {
        return;
    }

    for (slot = 0; slot < ACGC_GBI_REFERENCE_REGISTRY_CAPACITY; slot++) {
        registry->entries[slot].value = 0;
        registry->entries[slot].occupied = 0;
        registry->entries[slot].generation = next_generation(
            registry->entries[slot].generation
        );
    }
    registry->next_slot = 0;
}

AcgcGbiReferenceStatus acgc_gbi_reference_registry_register(
    AcgcGbiReferenceRegistry* registry,
    uintptr_t value,
    uint32_t* out_handle
) {
    uint32_t slot;
    uint32_t offset;

    if (registry == NULL || out_handle == NULL) {
        return ACGC_GBI_REFERENCE_INVALID_ARGUMENT;
    }
    *out_handle = 0;

    for (slot = 0; slot < ACGC_GBI_REFERENCE_REGISTRY_CAPACITY; slot++) {
        const AcgcGbiReferenceEntry* entry = &registry->entries[slot];

        if (entry->occupied != 0 && entry->value == value) {
            *out_handle = make_handle(slot, entry->generation);
            return ACGC_GBI_REFERENCE_OK;
        }
    }

    for (offset = 0; offset < ACGC_GBI_REFERENCE_REGISTRY_CAPACITY; offset++) {
        slot = (registry->next_slot + offset) % ACGC_GBI_REFERENCE_REGISTRY_CAPACITY;
        if (registry->entries[slot].occupied == 0) {
            AcgcGbiReferenceEntry* entry = &registry->entries[slot];

            if (entry->generation == 0) {
                entry->generation = UINT16_C(1);
            }
            entry->value = value;
            entry->occupied = 1;
            registry->next_slot = (slot + 1) % ACGC_GBI_REFERENCE_REGISTRY_CAPACITY;
            *out_handle = make_handle(slot, entry->generation);
            return ACGC_GBI_REFERENCE_OK;
        }
    }

    return ACGC_GBI_REFERENCE_EXHAUSTED;
}

AcgcGbiReferenceStatus acgc_gbi_reference_registry_resolve(
    const AcgcGbiReferenceRegistry* registry,
    uint32_t handle,
    uintptr_t* out_value
) {
    uint32_t slot;
    uint16_t generation;
    AcgcGbiReferenceStatus status;
    const AcgcGbiReferenceEntry* entry;

    if (registry == NULL || out_value == NULL) {
        return ACGC_GBI_REFERENCE_INVALID_ARGUMENT;
    }
    *out_value = 0;

    status = decode_handle(handle, &slot, &generation);
    if (status != ACGC_GBI_REFERENCE_OK) {
        return status;
    }

    if (slot >= ACGC_GBI_REFERENCE_REGISTRY_CAPACITY) {
        return ACGC_GBI_REFERENCE_INVALID_HANDLE;
    }

    entry = &registry->entries[slot];
    if (entry->occupied == 0 || entry->generation != generation) {
        return ACGC_GBI_REFERENCE_STALE_HANDLE;
    }

    *out_value = entry->value;
    return ACGC_GBI_REFERENCE_OK;
}

AcgcGbiReferenceStatus acgc_gbi_reference_registry_release(
    AcgcGbiReferenceRegistry* registry,
    uint32_t handle
) {
    uint32_t slot;
    uint16_t generation;
    AcgcGbiReferenceStatus status;
    AcgcGbiReferenceEntry* entry;

    if (registry == NULL) {
        return ACGC_GBI_REFERENCE_INVALID_ARGUMENT;
    }

    status = decode_handle(handle, &slot, &generation);
    if (status != ACGC_GBI_REFERENCE_OK) {
        return status;
    }

    if (slot >= ACGC_GBI_REFERENCE_REGISTRY_CAPACITY) {
        return ACGC_GBI_REFERENCE_INVALID_HANDLE;
    }

    entry = &registry->entries[slot];
    if (entry->occupied == 0 || entry->generation != generation) {
        return ACGC_GBI_REFERENCE_STALE_HANDLE;
    }

    entry->value = 0;
    entry->occupied = 0;
    entry->generation = next_generation(entry->generation);
    registry->next_slot = slot;
    return ACGC_GBI_REFERENCE_OK;
}

const char* acgc_gbi_reference_status_string(AcgcGbiReferenceStatus status) {
    switch (status) {
        case ACGC_GBI_REFERENCE_OK:
            return "ok";
        case ACGC_GBI_REFERENCE_INVALID_ARGUMENT:
            return "invalid argument";
        case ACGC_GBI_REFERENCE_INVALID_HANDLE:
            return "invalid handle";
        case ACGC_GBI_REFERENCE_STALE_HANDLE:
            return "stale handle";
        case ACGC_GBI_REFERENCE_EXHAUSTED:
            return "registry exhausted";
        default:
            return "unknown status";
    }
}
