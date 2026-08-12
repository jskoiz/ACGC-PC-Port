#ifndef ACGC_DVD_HOST_STATE_H
#define ACGC_DVD_HOST_STATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * These records are fixed GameCube wire/guest probes only.  Their pointer and
 * callback fields are raw 32-bit words; they must never be cast over the
 * TARGET_PC public DVDFileInfo/DVDCommandBlock types, whose pointer and
 * callback members are host-native and expand on LP64.  Host-owned state
 * belongs in AcgcDvdHostStateTable.
 */
typedef struct AcgcDvdDiskIdWire {
    char game_name[4];
    char company[2];
    uint8_t disk_number;
    uint8_t game_version;
    uint8_t streaming;
    uint8_t stream_buf_size;
    uint8_t padding[22];
} AcgcDvdDiskIdWire;

typedef struct AcgcDvdCommandBlockWire {
    uint32_t next;
    uint32_t prev;
    uint32_t command;
    int32_t state;
    uint32_t offset;
    uint32_t length;
    uint32_t addr;
    uint32_t curr_transfer_size;
    uint32_t transferred_size;
    uint32_t id;
    uint32_t callback;
    uint32_t user_data;
} AcgcDvdCommandBlockWire;

typedef struct AcgcDvdFileInfoWire {
    AcgcDvdCommandBlockWire cb;
    uint32_t start_addr;
    uint32_t length;
    uint32_t callback;
} AcgcDvdFileInfoWire;

typedef struct AcgcCardFileInfoLayout {
    int32_t chan;
    int32_t file_no;
    int32_t offset;
    int32_t length;
    uint16_t i_block;
} AcgcCardFileInfoLayout;

typedef struct AcgcCardDirLayout {
    uint8_t game_name[4];
    uint8_t company[2];
    uint8_t padding0;
    uint8_t banner_format;
    uint8_t file_name[32];
    uint32_t time;
    uint32_t icon_addr;
    uint16_t icon_format;
    uint16_t icon_speed;
    uint8_t permission;
    uint8_t copy_times;
    uint16_t start_block;
    uint16_t length;
    uint8_t padding1[2];
    uint32_t comment_addr;
} AcgcCardDirLayout;

#if defined(__cplusplus)
#define ACGC_DVD_LAYOUT_ASSERT(condition, message) static_assert((condition), message)
#else
#define ACGC_DVD_LAYOUT_ASSERT(condition, message) _Static_assert((condition), message)
#endif

ACGC_DVD_LAYOUT_ASSERT(sizeof(AcgcDvdDiskIdWire) == 0x20, "DVD disk ID wire size changed");
ACGC_DVD_LAYOUT_ASSERT(sizeof(AcgcDvdCommandBlockWire) == 0x30, "DVD command block wire size changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdCommandBlockWire, next) == 0x00, "DVD next wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdCommandBlockWire, prev) == 0x04, "DVD prev wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdCommandBlockWire, command) == 0x08, "DVD command wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdCommandBlockWire, state) == 0x0C, "DVD state wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdCommandBlockWire, offset) == 0x10, "DVD offset wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdCommandBlockWire, length) == 0x14, "DVD length wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdCommandBlockWire, addr) == 0x18, "DVD address wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdCommandBlockWire, curr_transfer_size) == 0x1C, "DVD current transfer wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdCommandBlockWire, transferred_size) == 0x20, "DVD transferred wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdCommandBlockWire, id) == 0x24, "DVD disk ID wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdCommandBlockWire, callback) == 0x28, "DVD callback wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdCommandBlockWire, user_data) == 0x2C, "DVD user data wire offset changed");

ACGC_DVD_LAYOUT_ASSERT(sizeof(AcgcDvdFileInfoWire) == 0x3C, "DVD file info wire size changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdFileInfoWire, cb) == 0x00, "DVD file command block wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdFileInfoWire, start_addr) == 0x30, "DVD file start address wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdFileInfoWire, length) == 0x34, "DVD file length wire offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcDvdFileInfoWire, callback) == 0x38, "DVD file callback wire offset changed");

ACGC_DVD_LAYOUT_ASSERT(sizeof(AcgcCardFileInfoLayout) == 0x14, "CARD file info size changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardFileInfoLayout, chan) == 0x00, "CARD channel offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardFileInfoLayout, file_no) == 0x04, "CARD file number offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardFileInfoLayout, offset) == 0x08, "CARD offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardFileInfoLayout, length) == 0x0C, "CARD length offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardFileInfoLayout, i_block) == 0x10, "CARD block offset changed");

ACGC_DVD_LAYOUT_ASSERT(sizeof(AcgcCardDirLayout) == 0x40, "CARD directory size changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, game_name) == 0x00, "CARD game name offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, company) == 0x04, "CARD company offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, padding0) == 0x06, "CARD padding offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, banner_format) == 0x07, "CARD banner offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, file_name) == 0x08, "CARD filename offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, time) == 0x28, "CARD time offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, icon_addr) == 0x2C, "CARD icon address offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, icon_format) == 0x30, "CARD icon format offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, icon_speed) == 0x32, "CARD icon speed offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, permission) == 0x34, "CARD permission offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, copy_times) == 0x35, "CARD copy count offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, start_block) == 0x36, "CARD start block offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, length) == 0x38, "CARD directory length offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, padding1) == 0x3A, "CARD trailing padding offset changed");
ACGC_DVD_LAYOUT_ASSERT(offsetof(AcgcCardDirLayout, comment_addr) == 0x3C, "CARD comment offset changed");

#undef ACGC_DVD_LAYOUT_ASSERT

#define ACGC_DVD_HOST_STATE_CAPACITY 512u
#define ACGC_DVD_HOST_HANDLE_PREFIX_MASK UINT32_C(0xF0000000)
#define ACGC_DVD_HOST_HANDLE_PREFIX UINT32_C(0xD0000000)
#define ACGC_DVD_HOST_HANDLE_SLOT_MASK UINT32_C(0x000001FF)
#define ACGC_DVD_HOST_HANDLE_GENERATION_MASK UINT32_C(0x0007FFFF)
#define ACGC_DVD_HOST_HANDLE_GENERATION_SHIFT 9u
#define ACGC_DVD_HOST_HANDLE_INVALID UINT32_C(0)
#define ACGC_DVD_HOST_PATH_CAPACITY 256u

typedef enum AcgcDvdHostSource {
    ACGC_DVD_HOST_SOURCE_FILE = 1,
    ACGC_DVD_HOST_SOURCE_DISC = 2
} AcgcDvdHostSource;

typedef struct AcgcDvdHostState {
    AcgcDvdHostSource source;
    void* host_file;
    uint32_t disc_offset;
    uint32_t length;
} AcgcDvdHostState;

typedef struct AcgcDvdHostStateEntry {
    AcgcDvdHostState state;
    const void* owner;
    uint32_t generation;
    uint8_t occupied;
} AcgcDvdHostStateEntry;

typedef struct AcgcDvdHostStateTable {
    AcgcDvdHostStateEntry entries[ACGC_DVD_HOST_STATE_CAPACITY];
    uint32_t next_slot;
} AcgcDvdHostStateTable;

typedef enum AcgcDvdHostStateStatus {
    ACGC_DVD_HOST_STATE_OK = 0,
    ACGC_DVD_HOST_STATE_INVALID_ARGUMENT,
    ACGC_DVD_HOST_STATE_INVALID_HANDLE,
    ACGC_DVD_HOST_STATE_STALE_HANDLE,
    ACGC_DVD_HOST_STATE_OWNER_NOT_FOUND,
    ACGC_DVD_HOST_STATE_DUPLICATE_OWNER,
    ACGC_DVD_HOST_STATE_EXHAUSTED
} AcgcDvdHostStateStatus;

typedef enum AcgcDvdHostPathStatus {
    ACGC_DVD_HOST_PATH_OK = 0,
    ACGC_DVD_HOST_PATH_INVALID_ARGUMENT,
    ACGC_DVD_HOST_PATH_EMPTY,
    ACGC_DVD_HOST_PATH_TOO_LONG
} AcgcDvdHostPathStatus;

void acgc_dvd_host_state_table_init(AcgcDvdHostStateTable* table);
/* Invalidates all live handles; callers must dispose host resources first. */
void acgc_dvd_host_state_table_reset(AcgcDvdHostStateTable* table);

AcgcDvdHostStateStatus acgc_dvd_host_state_allocate(
    AcgcDvdHostStateTable* table,
    const AcgcDvdHostState* state,
    uint32_t* out_handle
);

/* Install host state for one typed public DVD owner; duplicate owners fail. */
AcgcDvdHostStateStatus acgc_dvd_host_state_install_owner(
    AcgcDvdHostStateTable* table,
    const void* owner,
    const AcgcDvdHostState* state,
    uint32_t* out_handle
);

AcgcDvdHostStateStatus acgc_dvd_host_state_resolve(
    const AcgcDvdHostStateTable* table,
    uint32_t handle,
    AcgcDvdHostState* out_state
);

/* Resolve by the host-native public object address, never by a guest slot. */
AcgcDvdHostStateStatus acgc_dvd_host_state_resolve_owner(
    const AcgcDvdHostStateTable* table,
    const void* owner,
    uint32_t* out_handle,
    AcgcDvdHostState* out_state
);

AcgcDvdHostStateStatus acgc_dvd_host_state_release(
    AcgcDvdHostStateTable* table,
    uint32_t handle,
    AcgcDvdHostState* out_state
);

/* Release the state owned by one public object address. */
AcgcDvdHostStateStatus acgc_dvd_host_state_release_owner(
    AcgcDvdHostStateTable* table,
    const void* owner,
    AcgcDvdHostState* out_state
);

int acgc_dvd_host_state_read_range_valid(
    const AcgcDvdHostState* state,
    uint32_t offset,
    uint32_t length
);

AcgcDvdHostPathStatus acgc_dvd_host_path_copy(
    char* destination,
    size_t destination_size,
    const char* path
);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_DVD_HOST_STATE_H */
