/* pc_dvd.c - DVD filesystem: reads from disc image (CISO/ISO/GCM) or extracted files */
#include "pc_platform.h"
#include "pc_disc.h"
#include "dolphin/dvd.h"
#include "acgc/dvd_host_state.h"

/* The legacy PC shim also exports this helper as a function. */
#ifdef DVDGetFileInfoStatus
#undef DVDGetFileInfoStatus
#endif

/*
 * TARGET_PC public DVD ABI classification:
 *
 * - command/state/offset/length/transfer counters and DVDFileInfo's
 *   startAddr/length are fixed-width guest values;
 * - DVDCommandBlock next/prev/addr/id/userData and the file/command callback
 *   members are host-native pointers or function pointers and expand on LP64;
 * - FILE*, owner identity, and generational handles are host side-table state.
 *
 * The fixed AcgcDvd*Wire records are probes for the GameCube wire layout only.
 * No public DVDFileInfo or DVDCommandBlock is cast to one of those records.
 */

static DVDDiskID disk_id = {
    {'G', 'A', 'F', 'E'},
    {'0', '1'},
    0, 0,
    0, 0,
    {0}
};

DVDDiskID* DVDGetCurrentDiskID(void) { return &disk_id; }

#define MAX_DVD_ENTRIES ((int)ACGC_DVD_HOST_STATE_CAPACITY)

static struct {
    char path[ACGC_DVD_HOST_PATH_CAPACITY];
    int  used;
} dvd_entry_table[MAX_DVD_ENTRIES];
static int dvd_entry_count = 0;

static AcgcDvdHostStateTable dvd_host_state_table;
static int dvd_host_state_initialized = 0;

static void dvd_host_state_ensure_initialized(void) {
    if (!dvd_host_state_initialized) {
        acgc_dvd_host_state_table_init(&dvd_host_state_table);
        dvd_host_state_initialized = 1;
    }
}

static void dvd_fi_reset_public(DVDFileInfo* fileInfo) {
    memset(fileInfo, 0, sizeof(*fileInfo));
    fileInfo->cb.state = DVD_STATE_END;
}

static AcgcDvdHostStateStatus dvd_fi_resolve_state(
    const DVDFileInfo* fileInfo,
    AcgcDvdHostState* out_state
) {
    dvd_host_state_ensure_initialized();
    return acgc_dvd_host_state_resolve_owner(
        &dvd_host_state_table,
        fileInfo,
        NULL,
        out_state
    );
}

static BOOL dvd_fi_close_state(DVDFileInfo* fileInfo) {
    AcgcDvdHostState state;
    AcgcDvdHostStateStatus status;

    if (fileInfo == NULL) {
        return FALSE;
    }

    dvd_host_state_ensure_initialized();
    status = acgc_dvd_host_state_release_owner(
        &dvd_host_state_table,
        fileInfo,
        &state
    );
    if (status == ACGC_DVD_HOST_STATE_OWNER_NOT_FOUND) {
        /* Match the idempotent close behavior of the legacy PC shim. */
        return TRUE;
    }
    if (status != ACGC_DVD_HOST_STATE_OK) {
        return FALSE;
    }

    if (state.source == ACGC_DVD_HOST_SOURCE_FILE && state.host_file != NULL) {
        fclose((FILE*)state.host_file);
    }
    dvd_fi_reset_public(fileInfo);
    return TRUE;
}

static BOOL dvd_fi_install_state(
    DVDFileInfo* fileInfo,
    const AcgcDvdHostState* state
) {
    uint32_t handle;

    dvd_host_state_ensure_initialized();
    if (acgc_dvd_host_state_install_owner(
            &dvd_host_state_table,
            fileInfo,
            state,
            &handle
        ) != ACGC_DVD_HOST_STATE_OK) {
        return FALSE;
    }

    (void)handle;
    dvd_fi_reset_public(fileInfo);
    fileInfo->startAddr = state->disc_offset;
    fileInfo->length = state->length;
    return TRUE;
}

/* File-based fallback path (only used when no disc image) */
static char assets_base_path[512] = {0};
static int assets_fallback_inited = 0;

static void dvd_init_fallback_path(void) {
    if (assets_fallback_inited) return;
    assets_fallback_inited = 1;

    const char* candidates[] = {
        "assets/files",
        "assets",
        "../assets/files",
        "../assets",
        "../../assets/files",
        "../../assets",
    };
    for (int i = 0; i < (int)(sizeof(candidates)/sizeof(candidates[0])); i++) {
        char test[768];
        snprintf(test, sizeof(test), "%s/COPYDATE", candidates[i]);
        FILE* f = fopen(test, "rb");
        if (f) {
            fclose(f);
            strncpy(assets_base_path, candidates[i], sizeof(assets_base_path)-1);
            assets_base_path[sizeof(assets_base_path)-1] = '\0';
            return;
        }
    }
    strncpy(assets_base_path, "assets", sizeof(assets_base_path)-1);
    assets_base_path[sizeof(assets_base_path)-1] = '\0';
}

s32 DVDConvertPathToEntrynum(char* path) {
    char safe_path[ACGC_DVD_HOST_PATH_CAPACITY];

    if (acgc_dvd_host_path_copy(
            safe_path,
            sizeof(safe_path),
            path
        ) != ACGC_DVD_HOST_PATH_OK) {
        return -1;
    }

    for (int i = 0; i < dvd_entry_count; i++) {
        if (dvd_entry_table[i].used && strcmp(dvd_entry_table[i].path, safe_path) == 0) {
            return i;
        }
    }

    if (dvd_entry_count >= MAX_DVD_ENTRIES) {
        fprintf(stderr, "[PC/DVD] Entry table full (%d entries)! Cannot register: %s\n",
                MAX_DVD_ENTRIES, path);
        return -1;
    }

    int idx = dvd_entry_count++;
    memcpy(dvd_entry_table[idx].path, safe_path, strlen(safe_path) + 1);
    dvd_entry_table[idx].used = 1;
    return idx;
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo) {
    AcgcDvdHostState state;

    if (fileInfo == NULL) {
        return FALSE;
    }
    if (entrynum < 0 || entrynum >= dvd_entry_count || !dvd_entry_table[entrynum].used) {
        return FALSE;
    }

    const char* path = dvd_entry_table[entrynum].path;

    /* Try disc image first */
    if (pc_disc_is_open()) {
        u32 disc_off, disc_sz;
        if (pc_disc_find_file(path, &disc_off, &disc_sz)) {
            state.source = ACGC_DVD_HOST_SOURCE_DISC;
            state.host_file = NULL;
            state.disc_offset = disc_off;
            state.length = disc_sz;
            return dvd_fi_install_state(fileInfo, &state);
        }
    }

    /* Fall back to extracted files */
    dvd_init_fallback_path();
    {
        char fullpath[768];
        FILE* fp;
        u32 len;

        int written;

        if (path[0] == '/') {
            written = snprintf(fullpath, sizeof(fullpath), "%s%s", assets_base_path, path);
        } else {
            written = snprintf(fullpath, sizeof(fullpath), "%s/%s", assets_base_path, path);
        }
        if (written < 0 || (size_t)written >= sizeof(fullpath)) {
            return FALSE;
        }

        fp = fopen(fullpath, "rb");
        if (!fp) {
            return FALSE;
        }

        {
            long end;
            if (fseek(fp, 0, SEEK_END) != 0 ||
                (end = ftell(fp)) < 0 ||
                (uint64_t)end > UINT32_MAX ||
                fseek(fp, 0, SEEK_SET) != 0) {
                fclose(fp);
                return FALSE;
            }
            len = (u32)end;
        }

        state.source = ACGC_DVD_HOST_SOURCE_FILE;
        state.host_file = fp;
        state.disc_offset = 0;
        state.length = len;
        if (!dvd_fi_install_state(fileInfo, &state)) {
            fclose(fp);
            return FALSE;
        }
    }

    return TRUE;
}

BOOL DVDOpen(char* filename, DVDFileInfo* fileInfo) {
    if (fileInfo == NULL) return FALSE;
    s32 entry = DVDConvertPathToEntrynum(filename);
    if (entry < 0) return FALSE;
    return DVDFastOpen(entry, fileInfo);
}

BOOL DVDClose(DVDFileInfo* fileInfo) {
    return dvd_fi_close_state(fileInfo);
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* buf, s32 length, s32 offset, s32 prio) {
    AcgcDvdHostState state;
    uint64_t disc_offset;
    s32 result = -1;
    (void)prio;

    if (fileInfo == NULL || buf == NULL || length < 0 || offset < 0) {
        return -1;
    }
    if (dvd_fi_resolve_state(fileInfo, &state) != ACGC_DVD_HOST_STATE_OK) {
        return -1;
    }
    if (!acgc_dvd_host_state_read_range_valid(
            &state,
            (uint32_t)offset,
            (uint32_t)length
        )) {
        return -1;
    }

    fileInfo->cb.state = DVD_STATE_BUSY;
    fileInfo->cb.offset = (u32)offset;
    fileInfo->cb.length = (u32)length;
    fileInfo->cb.currTransferSize = 0;
    fileInfo->cb.transferredSize = 0;

    if (state.source == ACGC_DVD_HOST_SOURCE_DISC) {
        /* disc image read */
        disc_offset = (uint64_t)state.disc_offset + (uint32_t)offset;
        if (disc_offset > UINT32_MAX) {
            fileInfo->cb.state = DVD_STATE_FATAL_ERROR;
            return -1;
        }
        if (pc_disc_read((u32)disc_offset, buf, (u32)length))
            result = length;
    } else {
        FILE* fp = (FILE*)state.host_file;
        if (fp != NULL && fseek(fp, offset, SEEK_SET) == 0) {
            result = (s32)fread(buf, 1, (size_t)length, fp);
        }
    }

    fileInfo->cb.state = result >= 0 ? DVD_STATE_END : DVD_STATE_FATAL_ERROR;
    fileInfo->cb.currTransferSize = result >= 0 ? (u32)result : 0;
    fileInfo->cb.transferredSize = result >= 0 ? (u32)result : 0;
    return result;
}

s32 DVDRead(DVDFileInfo* fileInfo, void* buf, s32 length, s32 offset) {
    return DVDReadPrio(fileInfo, buf, length, offset, 2);
}

u32 DVDGetLength(DVDFileInfo* fileInfo) {
    AcgcDvdHostState state;

    if (fileInfo == NULL || dvd_fi_resolve_state(fileInfo, &state) != ACGC_DVD_HOST_STATE_OK) {
        return 0;
    }
    return (u32)state.length;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* buf, s32 length, s32 offset,
                      DVDCallback callback, s32 prio) {
    s32 nread;

    if (fileInfo == NULL) {
        return FALSE;
    }
    fileInfo->callback = callback;
    nread = DVDReadPrio(fileInfo, buf, length, offset, prio);
    if (callback) {
        callback(nread, fileInfo);
    }
    return TRUE;
}

void OSDVDFatalError(void) {
    fprintf(stderr, "[PC/DVD] Fatal DVD error\n");
}

void DVDInit(void) {
    /* disc image init is done in pc_main.c via pc_disc_init() */
    dvd_host_state_ensure_initialized();
}

void DVDSetAutoFatalMessaging(BOOL enable) { (void)enable; }

s32 DVDGetFileInfoStatus(DVDFileInfo* fileInfo) {
    if (fileInfo == NULL) {
        return DVD_STATE_FATAL_ERROR;
    }
    return fileInfo->cb.state;
}

s32 DVDGetTransferredSize(DVDFileInfo* fileInfo) {
    if (fileInfo == NULL) {
        return -1;
    }
    return (s32)fileInfo->cb.transferredSize;
}

BOOL DVDFastClose(DVDFileInfo* fileInfo) {
    return DVDClose(fileInfo);
}

s32 DVDGetDriveStatus(void) { return 0; }
s32 DVDCancel(volatile DVDCommandBlock* block) { (void)block; return 0; }
BOOL DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback) {
    (void)block; (void)callback; return TRUE;
}
s32 DVDChangeDisk(DVDCommandBlock* block, DVDDiskID* id) {
    (void)block; (void)id; return 0;
}
BOOL DVDChangeDiskAsync(DVDCommandBlock* block, DVDDiskID* id, DVDCBCallback callback) {
    (void)block; (void)id; (void)callback; return TRUE;
}
s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) {
    return block != NULL ? block->state : DVD_STATE_FATAL_ERROR;
}

BOOL DVDPrepareStreamAsync(DVDFileInfo* fi, u32 len, u32 off, DVDCallback cb) {
    (void)fi; (void)len; (void)off; (void)cb;
    return TRUE;
}
s32 DVDCancelStream(DVDCommandBlock* block) { (void)block; return 0; }
