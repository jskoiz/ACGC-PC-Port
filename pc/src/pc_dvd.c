/* pc_dvd.c - DVD filesystem: reads from disc image (CISO/ISO/GCM) or extracted files */
#include "pc_platform.h"
#include "pc_disc.h"
#include "acgc/dvd_host_state.h"

typedef AcgcDvdDiskIdLayout DVDDiskID;

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

static AcgcDvdFileInfoLayout* dvd_fi_layout(void* fileInfo) {
    return (AcgcDvdFileInfoLayout*)fileInfo;
}

static uint32_t dvd_fi_handle(const void* fileInfo) {
    return ((const AcgcDvdFileInfoLayout*)fileInfo)->cb.addr;
}

static void dvd_fi_set_handle(void* fileInfo, uint32_t handle) {
    dvd_fi_layout(fileInfo)->cb.addr = handle;
}

static BOOL dvd_fi_close_state(void* fileInfo) {
    AcgcDvdHostState state;
    uint32_t handle;
    AcgcDvdHostStateStatus status;

    if (fileInfo == NULL) {
        return FALSE;
    }

    dvd_host_state_ensure_initialized();
    handle = dvd_fi_handle(fileInfo);
    if (handle == ACGC_DVD_HOST_HANDLE_INVALID) {
        return TRUE;
    }

    status = acgc_dvd_host_state_release(
        &dvd_host_state_table,
        handle,
        &state
    );
    dvd_fi_set_handle(fileInfo, ACGC_DVD_HOST_HANDLE_INVALID);
    if (status != ACGC_DVD_HOST_STATE_OK) {
        return FALSE;
    }

    if (state.source == ACGC_DVD_HOST_SOURCE_FILE && state.host_file != NULL) {
        fclose((FILE*)state.host_file);
    }
    return TRUE;
}

static BOOL dvd_fi_install_state(
    void* fileInfo,
    const AcgcDvdHostState* state
) {
    AcgcDvdFileInfoLayout* layout;
    uint32_t handle;

    dvd_host_state_ensure_initialized();
    if (acgc_dvd_host_state_allocate(
            &dvd_host_state_table,
            state,
            &handle
        ) != ACGC_DVD_HOST_STATE_OK) {
        return FALSE;
    }

    memset(fileInfo, 0, sizeof(AcgcDvdFileInfoLayout));
    layout = dvd_fi_layout(fileInfo);
    layout->cb.addr = handle;
    layout->start_addr = state->disc_offset;
    layout->length = state->length;
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

s32 DVDConvertPathToEntrynum(const char* path) {
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

BOOL DVDFastOpen(s32 entrynum, void* fileInfo) {
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

BOOL DVDOpen(const char* filename, void* fileInfo) {
    if (fileInfo == NULL) return FALSE;
    s32 entry = DVDConvertPathToEntrynum(filename);
    if (entry < 0) return FALSE;
    return DVDFastOpen(entry, fileInfo);
}

BOOL DVDClose(void* fileInfo) {
    return dvd_fi_close_state(fileInfo);
}

s32 DVDReadPrio(void* fileInfo, void* buf, s32 length, s32 offset, s32 prio) {
    AcgcDvdHostState state;
    uint64_t disc_offset;
    (void)prio;

    if (fileInfo == NULL || buf == NULL || length < 0 || offset < 0) {
        return -1;
    }
    if (acgc_dvd_host_state_resolve(
            &dvd_host_state_table,
            dvd_fi_handle(fileInfo),
            &state
        ) != ACGC_DVD_HOST_STATE_OK) {
        return -1;
    }
    if (!acgc_dvd_host_state_read_range_valid(
            &state,
            (uint32_t)offset,
            (uint32_t)length
        )) {
        return -1;
    }

    if (state.source == ACGC_DVD_HOST_SOURCE_DISC) {
        /* disc image read */
        disc_offset = (uint64_t)state.disc_offset + (uint32_t)offset;
        if (disc_offset > UINT32_MAX) {
            return -1;
        }
        if (pc_disc_read((u32)disc_offset, buf, (u32)length))
            return length;
        return -1;
    }

    FILE* fp = (FILE*)state.host_file;
    if (!fp) {
        return -1;
    }

    fseek(fp, offset, SEEK_SET);
    return (s32)fread(buf, 1, length, fp);
}

s32 DVDRead(void* fileInfo, void* buf, s32 length, s32 offset) {
    return DVDReadPrio(fileInfo, buf, length, offset, 2);
}

u32 DVDGetLength(void* fileInfo) {
    AcgcDvdHostState state;

    if (fileInfo == NULL || acgc_dvd_host_state_resolve(
            &dvd_host_state_table,
            dvd_fi_handle(fileInfo),
            &state
        ) != ACGC_DVD_HOST_STATE_OK) {
        return 0;
    }
    return (u32)state.length;
}

typedef void (*pc_DVDCallback)(s32, void*);

BOOL DVDReadAsyncPrio(void* fileInfo, void* buf, s32 length, s32 offset,
                      pc_DVDCallback callback, s32 prio) {
    s32 nread = DVDReadPrio(fileInfo, buf, length, offset, prio);
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

s32 DVDGetFileInfoStatus(void* fileInfo) {
    (void)fileInfo;
    return 0;
}

s32 DVDGetTransferredSize(void* fileInfo) {
    (void)fileInfo;
    return 0;
}

BOOL DVDFastClose(void* fileInfo) {
    return DVDClose(fileInfo);
}

s32 DVDGetDriveStatus(void) { return 0; }
s32 DVDCancel(void* block) { (void)block; return 0; }
BOOL DVDCancelAsync(void* block, void* callback) { (void)block; (void)callback; return TRUE; }
s32 DVDChangeDisk(void* block, void* id) { (void)block; (void)id; return 0; }
BOOL DVDChangeDiskAsync(void* block, void* id, void* callback) { (void)block; (void)id; (void)callback; return TRUE; }
s32 DVDGetCommandBlockStatus(void* block) { (void)block; return 0; }

BOOL DVDPrepareStreamAsync(void* fi, u32 len, u32 off, void* cb) {
    (void)fi; (void)len; (void)off; (void)cb;
    return TRUE;
}
s32 DVDCancelStream(void* block) { (void)block; return 0; }
