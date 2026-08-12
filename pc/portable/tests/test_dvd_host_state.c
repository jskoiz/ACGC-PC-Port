#include "acgc/dvd_host_state.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int test_fixed_layout_words(void) {
    AcgcDvdFileInfoLayout dvd_info;
    AcgcCardFileInfoLayout card_info;
    AcgcCardDirLayout card_dir;
    uint32_t word;

    memset(&dvd_info, 0, sizeof(dvd_info));
    dvd_info.cb.addr = UINT32_C(0xD0000200);
    dvd_info.start_addr = UINT32_C(0x12345678);
    dvd_info.length = UINT32_C(0x87654321);
    memcpy(&word, (const uint8_t*)&dvd_info + 0x18, sizeof(word));
    CHECK(word == UINT32_C(0xD0000200));
    memcpy(&word, (const uint8_t*)&dvd_info + 0x30, sizeof(word));
    CHECK(word == UINT32_C(0x12345678));
    memcpy(&word, (const uint8_t*)&dvd_info + 0x34, sizeof(word));
    CHECK(word == UINT32_C(0x87654321));

    memset(&card_info, 0, sizeof(card_info));
    card_info.chan = -1;
    card_info.file_no = 7;
    card_info.i_block = UINT16_C(0x1234);
    CHECK(*(const int32_t*)((const uint8_t*)&card_info + 0x00) == -1);
    CHECK(*(const int32_t*)((const uint8_t*)&card_info + 0x04) == 7);
    CHECK(*(const uint16_t*)((const uint8_t*)&card_info + 0x10) == UINT16_C(0x1234));

    memset(&card_dir, 0, sizeof(card_dir));
    card_dir.file_name[0] = 'A';
    card_dir.comment_addr = UINT32_C(0xFFFFFFFF);
    CHECK(((const uint8_t*)&card_dir)[0x08] == 'A');
    memcpy(&word, (const uint8_t*)&card_dir + 0x3C, sizeof(word));
    CHECK(word == UINT32_C(0xFFFFFFFF));
    return 0;
}

static int test_allocate_release_and_stale_handles(void) {
    AcgcDvdHostStateTable table;
    AcgcDvdHostState requested;
    AcgcDvdHostState resolved;
    AcgcDvdHostState released;
    uint32_t first_handle = 0;
    uint32_t second_handle = 0;
    int marker = 0;

    acgc_dvd_host_state_table_init(&table);
    requested.source = ACGC_DVD_HOST_SOURCE_FILE;
    requested.host_file = &marker;
    requested.disc_offset = 0;
    requested.length = 123;

    CHECK(acgc_dvd_host_state_allocate(&table, &requested, &first_handle) ==
          ACGC_DVD_HOST_STATE_OK);
    CHECK((first_handle & ACGC_DVD_HOST_HANDLE_PREFIX_MASK) ==
          ACGC_DVD_HOST_HANDLE_PREFIX);
    CHECK(acgc_dvd_host_state_resolve(&table, first_handle, &resolved) ==
          ACGC_DVD_HOST_STATE_OK);
    CHECK(resolved.source == requested.source);
    CHECK(resolved.host_file == requested.host_file);
    CHECK(resolved.length == requested.length);

    CHECK(acgc_dvd_host_state_release(&table, first_handle, &released) ==
          ACGC_DVD_HOST_STATE_OK);
    CHECK(released.host_file == requested.host_file);
    CHECK(acgc_dvd_host_state_resolve(&table, first_handle, &resolved) ==
          ACGC_DVD_HOST_STATE_STALE_HANDLE);
    CHECK(acgc_dvd_host_state_release(&table, first_handle, NULL) ==
          ACGC_DVD_HOST_STATE_STALE_HANDLE);

    CHECK(acgc_dvd_host_state_allocate(&table, &requested, &second_handle) ==
          ACGC_DVD_HOST_STATE_OK);
    CHECK(second_handle != first_handle);
    CHECK(acgc_dvd_host_state_release(&table, second_handle, NULL) ==
          ACGC_DVD_HOST_STATE_OK);
    return 0;
}

static int test_exhaustion_and_reset(void) {
    static AcgcDvdHostStateTable table;
    uint32_t handles[ACGC_DVD_HOST_STATE_CAPACITY];
    AcgcDvdHostState state;
    AcgcDvdHostState resolved;
    uint32_t replacement = 0;
    uint32_t after_reset = 0;
    uint32_t i;

    acgc_dvd_host_state_table_init(&table);
    state.source = ACGC_DVD_HOST_SOURCE_DISC;
    state.host_file = NULL;
    state.disc_offset = 0;
    state.length = 1;
    for (i = 0; i < ACGC_DVD_HOST_STATE_CAPACITY; i++) {
        state.disc_offset = i;
        CHECK(acgc_dvd_host_state_allocate(&table, &state, &handles[i]) ==
              ACGC_DVD_HOST_STATE_OK);
    }

    CHECK(acgc_dvd_host_state_allocate(&table, &state, &replacement) ==
          ACGC_DVD_HOST_STATE_EXHAUSTED);
    CHECK(replacement == ACGC_DVD_HOST_HANDLE_INVALID);

    CHECK(acgc_dvd_host_state_release(&table, handles[ACGC_DVD_HOST_STATE_CAPACITY - 1u], NULL) ==
          ACGC_DVD_HOST_STATE_OK);
    CHECK(acgc_dvd_host_state_allocate(&table, &state, &replacement) ==
          ACGC_DVD_HOST_STATE_OK);
    CHECK(replacement != handles[ACGC_DVD_HOST_STATE_CAPACITY - 1u]);

    acgc_dvd_host_state_table_reset(&table);
    CHECK(acgc_dvd_host_state_resolve(&table, handles[0], &resolved) ==
          ACGC_DVD_HOST_STATE_STALE_HANDLE);
    CHECK(acgc_dvd_host_state_resolve(&table, replacement, &resolved) ==
          ACGC_DVD_HOST_STATE_STALE_HANDLE);
    CHECK(acgc_dvd_host_state_allocate(&table, &state, &after_reset) ==
          ACGC_DVD_HOST_STATE_OK);
    CHECK(after_reset != handles[0]);
    CHECK(acgc_dvd_host_state_release(&table, after_reset, NULL) ==
          ACGC_DVD_HOST_STATE_OK);
    return 0;
}

static int test_invalid_handles_and_states(void) {
    AcgcDvdHostStateTable table;
    AcgcDvdHostState state;
    AcgcDvdHostState resolved;
    uint32_t handle = UINT32_C(0xFFFFFFFF);
    int marker = 0;

    acgc_dvd_host_state_table_init(&table);
    state.source = ACGC_DVD_HOST_SOURCE_FILE;
    state.host_file = NULL;
    state.disc_offset = 0;
    state.length = 0;
    CHECK(acgc_dvd_host_state_allocate(&table, &state, &handle) ==
          ACGC_DVD_HOST_STATE_INVALID_ARGUMENT);
    state.host_file = &marker;
    CHECK(acgc_dvd_host_state_resolve(&table, ACGC_DVD_HOST_HANDLE_INVALID, &resolved) ==
          ACGC_DVD_HOST_STATE_INVALID_HANDLE);
    CHECK(acgc_dvd_host_state_resolve(&table, ACGC_DVD_HOST_HANDLE_PREFIX, &resolved) ==
          ACGC_DVD_HOST_STATE_INVALID_HANDLE);
    CHECK(acgc_dvd_host_state_resolve(
              &table,
              ACGC_DVD_HOST_HANDLE_PREFIX |
                  (UINT32_C(1) << ACGC_DVD_HOST_HANDLE_GENERATION_SHIFT) |
                  (ACGC_DVD_HOST_STATE_CAPACITY - 1u),
              &resolved
          ) == ACGC_DVD_HOST_STATE_STALE_HANDLE);
    CHECK(acgc_dvd_host_state_resolve(&table, UINT32_C(0xE0000200), &resolved) ==
          ACGC_DVD_HOST_STATE_INVALID_HANDLE);
    CHECK(acgc_dvd_host_state_allocate(&table, &state, NULL) ==
          ACGC_DVD_HOST_STATE_INVALID_ARGUMENT);
    return 0;
}

static int test_read_range_bounds(void) {
    AcgcDvdHostState state;

    memset(&state, 0, sizeof(state));
    state.length = 16;
    CHECK(acgc_dvd_host_state_read_range_valid(&state, 0, 0) == 1);
    CHECK(acgc_dvd_host_state_read_range_valid(&state, 0, 16) == 1);
    CHECK(acgc_dvd_host_state_read_range_valid(&state, 15, 1) == 1);
    CHECK(acgc_dvd_host_state_read_range_valid(&state, 16, 1) == 0);
    CHECK(acgc_dvd_host_state_read_range_valid(&state, 15, 2) == 0);
    CHECK(acgc_dvd_host_state_read_range_valid(&state, UINT32_MAX, 0) == 0);
    CHECK(acgc_dvd_host_state_read_range_valid(NULL, 0, 0) == 0);
    return 0;
}

static int test_bounded_paths(void) {
    char destination[8];
    char exact[sizeof(destination)];
    char oversized[sizeof(destination) + 1u];

    memset(exact, 'x', sizeof(exact));
    exact[sizeof(exact) - 1u] = '\0';
    memset(oversized, 'y', sizeof(oversized));
    oversized[sizeof(oversized) - 1u] = '\0';

    CHECK(acgc_dvd_host_path_copy(destination, sizeof(destination), "abc") ==
          ACGC_DVD_HOST_PATH_OK);
    CHECK(strcmp(destination, "abc") == 0);
    CHECK(acgc_dvd_host_path_copy(destination, sizeof(destination), exact) ==
          ACGC_DVD_HOST_PATH_OK);
    CHECK(acgc_dvd_host_path_copy(destination, sizeof(destination), oversized) ==
          ACGC_DVD_HOST_PATH_TOO_LONG);
    CHECK(acgc_dvd_host_path_copy(destination, sizeof(destination), "") ==
          ACGC_DVD_HOST_PATH_EMPTY);
    CHECK(acgc_dvd_host_path_copy(destination, sizeof(destination), NULL) ==
          ACGC_DVD_HOST_PATH_INVALID_ARGUMENT);
    CHECK(acgc_dvd_host_path_copy(NULL, sizeof(destination), "abc") ==
          ACGC_DVD_HOST_PATH_INVALID_ARGUMENT);
    CHECK(acgc_dvd_host_path_copy(destination, 0, "abc") ==
          ACGC_DVD_HOST_PATH_INVALID_ARGUMENT);
    return 0;
}

int main(void) {
    CHECK(test_fixed_layout_words() == 0);
    CHECK(test_allocate_release_and_stale_handles() == 0);
    CHECK(test_exhaustion_and_reset() == 0);
    CHECK(test_invalid_handles_and_states() == 0);
    CHECK(test_read_range_bounds() == 0);
    CHECK(test_bounded_paths() == 0);
    printf("acgc DVD host-state tests passed\n");
    return 0;
}
