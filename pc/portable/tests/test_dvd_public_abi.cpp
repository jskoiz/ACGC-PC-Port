#define TARGET_PC 1

#include "types.h"
#include "dolphin/dvd.h"
#include "acgc/dvd_host_state.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

static bool check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, line, expression);
        return false;
    }
    return true;
}

#define CHECK(condition) do { \
    if (!check((condition), #condition, __LINE__)) return 1; \
} while (0)

#if UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(DVDCommandBlock) == 0x48,
              "C++ must see the native LP64 command block");
static_assert(sizeof(DVDFileInfo) == 0x58,
              "C++ must see the native LP64 file info");
static_assert(sizeof(DVDFileInfo) != sizeof(AcgcDvdFileInfoWire),
              "C++ public file info must not alias the wire probe");
static_assert(offsetof(DVDCommandBlock, addr) == 0x20,
              "C++ addr classification changed");
static_assert(offsetof(DVDFileInfo, callback) == 0x50,
              "C++ callback classification changed");
#elif UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(DVDCommandBlock) == sizeof(AcgcDvdCommandBlockWire),
              "C++ ILP32 command block must retain the wire size");
static_assert(sizeof(DVDFileInfo) == sizeof(AcgcDvdFileInfoWire),
              "C++ ILP32 file info must retain the wire size");
static_assert(offsetof(DVDCommandBlock, addr) ==
                  offsetof(AcgcDvdCommandBlockWire, addr),
              "C++ ILP32 addr must retain the wire offset");
static_assert(offsetof(DVDFileInfo, length) ==
                  offsetof(AcgcDvdFileInfoWire, length),
              "C++ ILP32 file length must retain the wire offset");
#endif

static int callback_count;
static DVDFileInfo* callback_info;

static void typed_callback(s32, DVDFileInfo* file_info) {
    callback_count++;
    callback_info = file_info;
}

static int test_cpp_typed_owner(void) {
    AcgcDvdHostStateTable table{};
    AcgcDvdHostState state{};
    AcgcDvdHostState resolved{};
    DVDFileInfo file_info{};
    std::uint32_t handle = ACGC_DVD_HOST_HANDLE_INVALID;
    int marker = 0;

    state.source = ACGC_DVD_HOST_SOURCE_FILE;
    state.host_file = &marker;
    state.length = 64;
    acgc_dvd_host_state_table_init(&table);
    CHECK(acgc_dvd_host_state_install_owner(
              &table, &file_info, &state, &handle
          ) == ACGC_DVD_HOST_STATE_OK);
    CHECK(acgc_dvd_host_state_resolve_owner(
              &table, &file_info, nullptr, &resolved
          ) == ACGC_DVD_HOST_STATE_OK);
    CHECK(resolved.length == 64);
    CHECK(acgc_dvd_host_state_release_owner(&table, &file_info, nullptr) ==
          ACGC_DVD_HOST_STATE_OK);
    CHECK(acgc_dvd_host_state_resolve_owner(
              &table, &file_info, nullptr, &resolved
          ) == ACGC_DVD_HOST_STATE_OWNER_NOT_FOUND);
    CHECK(acgc_dvd_host_state_resolve(&table, handle, &resolved) ==
          ACGC_DVD_HOST_STATE_STALE_HANDLE);
    return 0;
}

static int test_cpp_typed_callback(void) {
    DVDFileInfo file_info{};
    DVDCallback callback = typed_callback;

    callback_count = 0;
    callback_info = nullptr;
    callback(-1, &file_info);
    CHECK(callback_count == 1);
    CHECK(callback_info == &file_info);
    return 0;
}

int main() {
    CHECK(test_cpp_typed_owner() == 0);
    CHECK(test_cpp_typed_callback() == 0);
    std::puts("acgc typed DVD public C++ ABI tests passed");
    return 0;
}
