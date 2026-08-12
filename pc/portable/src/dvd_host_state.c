#include "acgc/dvd_host_state.h"

#include <string.h>

_Static_assert(
    ACGC_DVD_HOST_STATE_CAPACITY <= ACGC_DVD_HOST_HANDLE_SLOT_MASK + 1u,
    "DVD host-state capacity does not fit its handle slot field"
);

static uint32_t next_generation(uint32_t generation) {
    uint32_t next = (generation & ACGC_DVD_HOST_HANDLE_GENERATION_MASK) + 1u;

    if (next > ACGC_DVD_HOST_HANDLE_GENERATION_MASK) {
        next = 1u;
    }
    return next;
}

static uint32_t make_handle(uint32_t slot, uint32_t generation) {
    return ACGC_DVD_HOST_HANDLE_PREFIX |
           ((generation & ACGC_DVD_HOST_HANDLE_GENERATION_MASK)
            << ACGC_DVD_HOST_HANDLE_GENERATION_SHIFT) |
           (slot & ACGC_DVD_HOST_HANDLE_SLOT_MASK);
}

static AcgcDvdHostStateStatus decode_handle(
    uint32_t handle,
    uint32_t* out_slot,
    uint32_t* out_generation
) {
    uint32_t generation;
    uint32_t slot;

    if ((handle & ACGC_DVD_HOST_HANDLE_PREFIX_MASK) !=
        ACGC_DVD_HOST_HANDLE_PREFIX) {
        return ACGC_DVD_HOST_STATE_INVALID_HANDLE;
    }

    generation = (handle >> ACGC_DVD_HOST_HANDLE_GENERATION_SHIFT) &
                 ACGC_DVD_HOST_HANDLE_GENERATION_MASK;
    slot = handle & ACGC_DVD_HOST_HANDLE_SLOT_MASK;
    if (generation == 0u || slot >= ACGC_DVD_HOST_STATE_CAPACITY) {
        return ACGC_DVD_HOST_STATE_INVALID_HANDLE;
    }

    *out_slot = slot;
    *out_generation = generation;
    return ACGC_DVD_HOST_STATE_OK;
}

static int valid_state(const AcgcDvdHostState* state) {
    if (state->source == ACGC_DVD_HOST_SOURCE_FILE) {
        return state->host_file != NULL;
    }
    return state->source == ACGC_DVD_HOST_SOURCE_DISC;
}

void acgc_dvd_host_state_table_init(AcgcDvdHostStateTable* table) {
    uint32_t slot;

    if (table == NULL) {
        return;
    }

    memset(table, 0, sizeof(*table));
    for (slot = 0; slot < ACGC_DVD_HOST_STATE_CAPACITY; slot++) {
        table->entries[slot].generation = 1u;
    }
}

void acgc_dvd_host_state_table_reset(AcgcDvdHostStateTable* table) {
    uint32_t slot;

    if (table == NULL) {
        return;
    }

    for (slot = 0; slot < ACGC_DVD_HOST_STATE_CAPACITY; slot++) {
        table->entries[slot].state.source = 0;
        table->entries[slot].state.host_file = NULL;
        table->entries[slot].state.disc_offset = 0;
        table->entries[slot].state.length = 0;
        table->entries[slot].occupied = 0;
        table->entries[slot].generation = next_generation(
            table->entries[slot].generation
        );
    }
    table->next_slot = 0;
}

AcgcDvdHostStateStatus acgc_dvd_host_state_allocate(
    AcgcDvdHostStateTable* table,
    const AcgcDvdHostState* state,
    uint32_t* out_handle
) {
    uint32_t offset;

    if (table == NULL || state == NULL || out_handle == NULL ||
        !valid_state(state)) {
        return ACGC_DVD_HOST_STATE_INVALID_ARGUMENT;
    }
    *out_handle = ACGC_DVD_HOST_HANDLE_INVALID;

    for (offset = 0; offset < ACGC_DVD_HOST_STATE_CAPACITY; offset++) {
        uint32_t slot = (table->next_slot + offset) % ACGC_DVD_HOST_STATE_CAPACITY;
        AcgcDvdHostStateEntry* entry = &table->entries[slot];

        if (entry->occupied == 0) {
            if (entry->generation == 0u) {
                entry->generation = 1u;
            }
            entry->state = *state;
            entry->occupied = 1;
            table->next_slot = (slot + 1u) % ACGC_DVD_HOST_STATE_CAPACITY;
            *out_handle = make_handle(slot, entry->generation);
            return ACGC_DVD_HOST_STATE_OK;
        }
    }

    return ACGC_DVD_HOST_STATE_EXHAUSTED;
}

AcgcDvdHostStateStatus acgc_dvd_host_state_resolve(
    const AcgcDvdHostStateTable* table,
    uint32_t handle,
    AcgcDvdHostState* out_state
) {
    uint32_t slot;
    uint32_t generation;
    AcgcDvdHostStateStatus status;
    const AcgcDvdHostStateEntry* entry;

    if (table == NULL || out_state == NULL) {
        return ACGC_DVD_HOST_STATE_INVALID_ARGUMENT;
    }
    memset(out_state, 0, sizeof(*out_state));

    status = decode_handle(handle, &slot, &generation);
    if (status != ACGC_DVD_HOST_STATE_OK) {
        return status;
    }

    entry = &table->entries[slot];
    if (entry->occupied == 0 || entry->generation != generation) {
        return ACGC_DVD_HOST_STATE_STALE_HANDLE;
    }

    *out_state = entry->state;
    return ACGC_DVD_HOST_STATE_OK;
}

AcgcDvdHostStateStatus acgc_dvd_host_state_release(
    AcgcDvdHostStateTable* table,
    uint32_t handle,
    AcgcDvdHostState* out_state
) {
    uint32_t slot;
    uint32_t generation;
    AcgcDvdHostStateStatus status;
    AcgcDvdHostStateEntry* entry;

    if (table == NULL) {
        return ACGC_DVD_HOST_STATE_INVALID_ARGUMENT;
    }
    if (out_state != NULL) {
        memset(out_state, 0, sizeof(*out_state));
    }

    status = decode_handle(handle, &slot, &generation);
    if (status != ACGC_DVD_HOST_STATE_OK) {
        return status;
    }

    entry = &table->entries[slot];
    if (entry->occupied == 0 || entry->generation != generation) {
        return ACGC_DVD_HOST_STATE_STALE_HANDLE;
    }

    if (out_state != NULL) {
        *out_state = entry->state;
    }
    memset(&entry->state, 0, sizeof(entry->state));
    entry->occupied = 0;
    entry->generation = next_generation(entry->generation);
    table->next_slot = slot;
    return ACGC_DVD_HOST_STATE_OK;
}

int acgc_dvd_host_state_read_range_valid(
    const AcgcDvdHostState* state,
    uint32_t offset,
    uint32_t length
) {
    if (state == NULL || (uint64_t)offset > (uint64_t)state->length) {
        return 0;
    }
    return (uint64_t)length <= (uint64_t)state->length - (uint64_t)offset;
}

AcgcDvdHostPathStatus acgc_dvd_host_path_copy(
    char* destination,
    size_t destination_size,
    const char* path
) {
    size_t path_length;

    if (destination == NULL || destination_size == 0 || path == NULL) {
        if (destination != NULL && destination_size != 0) {
            destination[0] = '\0';
        }
        return ACGC_DVD_HOST_PATH_INVALID_ARGUMENT;
    }

    destination[0] = '\0';
    if (path[0] == '\0') {
        return ACGC_DVD_HOST_PATH_EMPTY;
    }

    path_length = strlen(path);
    if (path_length >= destination_size) {
        return ACGC_DVD_HOST_PATH_TOO_LONG;
    }

    memcpy(destination, path, path_length + 1u);
    return ACGC_DVD_HOST_PATH_OK;
}
