#define TARGET_PC 1

#include "types.h"
#include "dolphin/dvd.h"
#include "acgc/dvd_host_state.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(DVDCommandBlock) == 0x48,
               "LP64 DVDCommandBlock must expose native pointer slots");
_Static_assert(offsetof(DVDCommandBlock, command) == 0x10,
               "LP64 command word must follow two native pointers");
_Static_assert(offsetof(DVDCommandBlock, offset) == 0x18,
               "LP64 offset word classification changed");
_Static_assert(offsetof(DVDCommandBlock, addr) == 0x20,
               "LP64 addr must be a native pointer member");
_Static_assert(offsetof(DVDCommandBlock, callback) == 0x38,
               "LP64 command callback must be a native function pointer");
_Static_assert(sizeof(DVDFileInfo) == 0x58,
               "LP64 DVDFileInfo must expose the expanded command block");
_Static_assert(sizeof(DVDFileInfo) != sizeof(AcgcDvdFileInfoWire),
               "LP64 public DVDFileInfo must not alias the wire probe");
_Static_assert(offsetof(DVDFileInfo, startAddr) == 0x48,
               "LP64 startAddr follows the native command block");
_Static_assert(offsetof(DVDFileInfo, length) == 0x4C,
               "LP64 file length remains a guest word");
_Static_assert(offsetof(DVDFileInfo, callback) == 0x50,
               "LP64 file callback must be a native function pointer");
_Static_assert(offsetof(DVDCommandBlock, addr) !=
                   offsetof(AcgcDvdCommandBlockWire, addr),
               "LP64 addr must not be read from the wire offset");
_Static_assert(sizeof(((DVDCommandBlock*)0)->next) == sizeof(void*),
               "DVD next is host-native pointer state");
_Static_assert(sizeof(((DVDCommandBlock*)0)->addr) == sizeof(void*),
               "DVD addr is host-native pointer state");
_Static_assert(sizeof(((DVDCommandBlock*)0)->callback) == sizeof(void (*)(void)),
               "DVD command callback is host-native callback state");
_Static_assert(sizeof(((DVDFileInfo*)0)->callback) == sizeof(void (*)(void)),
               "DVD file callback is host-native callback state");
#elif UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(DVDCommandBlock) == sizeof(AcgcDvdCommandBlockWire),
               "ILP32 public command block must retain the wire size");
_Static_assert(sizeof(DVDFileInfo) == sizeof(AcgcDvdFileInfoWire),
               "ILP32 public file info must retain the wire size");
_Static_assert(offsetof(DVDCommandBlock, addr) ==
                   offsetof(AcgcDvdCommandBlockWire, addr),
               "ILP32 addr must retain the wire offset");
_Static_assert(offsetof(DVDFileInfo, length) ==
                   offsetof(AcgcDvdFileInfoWire, length),
               "ILP32 file length must retain the wire offset");
#endif

static int callback_count;
static s32 callback_result;
static DVDFileInfo* callback_info;

static void typed_read_callback(s32 result, DVDFileInfo* file_info) {
    callback_count++;
    callback_result = result;
    callback_info = file_info;
}

static void dispatch_typed_callback(
    DVDCallback callback,
    s32 result,
    DVDFileInfo* file_info
) {
    callback(result, file_info);
}

static int test_typed_owner_lifecycle(void) {
    AcgcDvdHostStateTable table;
    AcgcDvdHostState requested;
    AcgcDvdHostState resolved;
    AcgcDvdHostState released;
    DVDFileInfo first;
    DVDFileInfo second;
    uint32_t first_handle = ACGC_DVD_HOST_HANDLE_INVALID;
    uint32_t duplicate_handle = UINT32_C(0xFFFFFFFF);
    uint32_t second_handle = ACGC_DVD_HOST_HANDLE_INVALID;
    int marker = 0;

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    memset(&requested, 0, sizeof(requested));
    requested.source = ACGC_DVD_HOST_SOURCE_FILE;
    requested.host_file = &marker;
    requested.length = 32;
    acgc_dvd_host_state_table_init(&table);

    CHECK(acgc_dvd_host_state_resolve_owner(
              &table, &first, NULL, &resolved
          ) == ACGC_DVD_HOST_STATE_OWNER_NOT_FOUND);
    CHECK(acgc_dvd_host_state_install_owner(
              &table, &first, &requested, &first_handle
          ) == ACGC_DVD_HOST_STATE_OK);
    CHECK(first_handle != ACGC_DVD_HOST_HANDLE_INVALID);
    CHECK(acgc_dvd_host_state_resolve_owner(
              &table, &first, &duplicate_handle, &resolved
          ) == ACGC_DVD_HOST_STATE_OK);
    CHECK(duplicate_handle == first_handle);
    CHECK(resolved.host_file == requested.host_file);
    CHECK(acgc_dvd_host_state_install_owner(
              &table, &first, &requested, &duplicate_handle
          ) == ACGC_DVD_HOST_STATE_DUPLICATE_OWNER);
    CHECK(duplicate_handle == ACGC_DVD_HOST_HANDLE_INVALID);

    CHECK(acgc_dvd_host_state_release_owner(
              &table, &first, &released
          ) == ACGC_DVD_HOST_STATE_OK);
    CHECK(released.host_file == requested.host_file);
    CHECK(acgc_dvd_host_state_resolve_owner(
              &table, &first, NULL, &resolved
          ) == ACGC_DVD_HOST_STATE_OWNER_NOT_FOUND);
    CHECK(acgc_dvd_host_state_release_owner(
              &table, &first, NULL
          ) == ACGC_DVD_HOST_STATE_OWNER_NOT_FOUND);
    CHECK(acgc_dvd_host_state_resolve(
              &table, first_handle, &resolved
          ) == ACGC_DVD_HOST_STATE_STALE_HANDLE);

    CHECK(acgc_dvd_host_state_install_owner(
              &table, &second, &requested, &second_handle
          ) == ACGC_DVD_HOST_STATE_OK);
    CHECK(second_handle != first_handle);
    CHECK(acgc_dvd_host_state_release_owner(&table, &second, NULL) ==
          ACGC_DVD_HOST_STATE_OK);
    return 0;
}

static int test_uninitialized_owner_and_bounds(void) {
    AcgcDvdHostStateTable table;
    AcgcDvdHostState state;
    AcgcDvdHostState resolved;
    DVDFileInfo uninitialized;
    int marker = 0;

    memset(&uninitialized, 0, sizeof(uninitialized));
    memset(&state, 0, sizeof(state));
    state.source = ACGC_DVD_HOST_SOURCE_FILE;
    state.host_file = &marker;
    state.length = 16;
    acgc_dvd_host_state_table_init(&table);

    CHECK(acgc_dvd_host_state_resolve_owner(
              &table, &uninitialized, NULL, &resolved
          ) == ACGC_DVD_HOST_STATE_OWNER_NOT_FOUND);
    CHECK(acgc_dvd_host_state_release_owner(
              &table, &uninitialized, NULL
          ) == ACGC_DVD_HOST_STATE_OWNER_NOT_FOUND);
    CHECK(acgc_dvd_host_state_read_range_valid(&state, 0, 16) == 1);
    CHECK(acgc_dvd_host_state_read_range_valid(&state, 15, 1) == 1);
    CHECK(acgc_dvd_host_state_read_range_valid(&state, 16, 1) == 0);
    CHECK(acgc_dvd_host_state_read_range_valid(&state, 15, 2) == 0);
    CHECK(acgc_dvd_host_state_read_range_valid(&state, UINT32_MAX, 0) == 0);
    return 0;
}

static int test_typed_callback(void) {
    DVDFileInfo file_info;

    memset(&file_info, 0, sizeof(file_info));
    callback_count = 0;
    callback_result = 0;
    callback_info = NULL;
    dispatch_typed_callback(typed_read_callback, -3, &file_info);
    CHECK(callback_count == 1);
    CHECK(callback_result == -3);
    CHECK(callback_info == &file_info);
    return 0;
}

int main(void) {
    CHECK(test_typed_owner_lifecycle() == 0);
    CHECK(test_uninitialized_owner_and_bounds() == 0);
    CHECK(test_typed_callback() == 0);
    printf("acgc typed DVD public C ABI tests passed\n");
    return 0;
}
