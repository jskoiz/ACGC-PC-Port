/* Read files from GC disc images (CISO/ISO/GCM)
 * Used by pc_assets.c for DOL+REL extraction and pc_dvd.c for runtime file reads. */
#ifdef TARGET_PC
#if !defined(_WIN32)
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#if !defined(_WIN32)
#include <sys/types.h>
#endif
#include <stdint.h>
#include "types.h"
#include "acgc/bytes.h"
#include "acgc/disc.h"
#include "pc_disc.h"

extern int g_pc_verbose;

#define CISO_HDR_SIZE ((size_t)ACGC_DISC_CISO_HEADER_SIZE)

typedef struct {
    FILE* fp;
    int is_ciso;
    u32 logical_size;
    AcgcCisoMap ciso;
} DiscFile;

/* ---- global state ---- */
static DiscFile g_disc;
static int g_disc_open = 0;

/* DOL info */
static u32 g_dol_offset = 0;
static u32 g_dol_size = 0;

/* FST file table */
#define MAX_FST_FILES 1024
typedef struct {
    char path[256];
    u32 disc_offset;
    u32 file_size;
} FSTFile;
static FSTFile g_fst_files[MAX_FST_FILES];
static int g_fst_file_count = 0;

/* ---- disc I/O ---- */
static int disc_seek(FILE* fp, uint64_t offset, int whence) {
#if defined(_WIN32)
    if (offset > (uint64_t)INT64_MAX) {
        return -1;
    }
    return _fseeki64(fp, (int64_t)offset, whence);
#else
    off_t seek_offset = (off_t)offset;

    if (seek_offset < 0 || (uint64_t)seek_offset != offset) {
        return -1;
    }
    return fseeko(fp, seek_offset, whence);
#endif
}

static int disc_get_file_size(FILE* fp, uint64_t* file_size) {
#if defined(_WIN32)
    int64_t end;
#else
    off_t end;
#endif

    if (fp == NULL || file_size == NULL || disc_seek(fp, 0, SEEK_END) != 0) {
        return 0;
    }
#if defined(_WIN32)
    end = _ftelli64(fp);
#else
    end = ftello(fp);
#endif
    if (end < 0 || disc_seek(fp, 0, SEEK_SET) != 0) {
        return 0;
    }
    *file_size = (uint64_t)end;
    return 1;
}

static int disc_host_read(
    void* context,
    uint64_t offset,
    void* destination,
    size_t size
) {
    DiscFile* df = (DiscFile*)context;
    uint64_t host_max = (uint64_t)INT64_MAX;

    if (df == NULL || df->fp == NULL ||
        offset > host_max || (uint64_t)size > host_max - offset) {
        return 0;
    }
    if (disc_seek(df->fp, offset, SEEK_SET) != 0) {
        return 0;
    }
    return fread(destination, 1, size, df->fp) == size;
}

static int disc_open(DiscFile* df, const char* path) {
    u8 hdr[CISO_HDR_SIZE];
    uint64_t physical_size;
    size_t header_read;
    AcgcDiscStatus status;

    memset(df, 0, sizeof(*df));
    df->fp = fopen(path, "rb");
    if (!df->fp) return 0;
    if (!disc_get_file_size(df->fp, &physical_size)) {
        fclose(df->fp);
        memset(df, 0, sizeof(*df));
        return 0;
    }

    header_read = fread(hdr, 1, CISO_HDR_SIZE, df->fp);
    if (header_read >= sizeof(uint32_t) &&
        acgc_load_le32(hdr) == UINT32_C(0x4F534943)) {
        status = acgc_ciso_parse(hdr, header_read, physical_size, &df->ciso);
        if (status != ACGC_DISC_OK || df->ciso.logical_size > UINT32_MAX) {
            acgc_ciso_dispose(&df->ciso);
            fclose(df->fp);
            memset(df, 0, sizeof(*df));
            return 0;
        }
        df->logical_size = (u32)df->ciso.logical_size;
        df->is_ciso = 1;
        return 1;
    }

    /* plain ISO/GCM */
    if (physical_size > UINT32_MAX) {
        fclose(df->fp);
        memset(df, 0, sizeof(*df));
        return 0;
    }
    df->logical_size = (u32)physical_size;
    df->is_ciso = 0;
    return 1;
}

static void disc_close(DiscFile* df) {
    if (df->fp) fclose(df->fp);
    acgc_ciso_dispose(&df->ciso);
    memset(df, 0, sizeof(*df));
}

static int disc_read(DiscFile* df, u32 offset, void* dest, u32 size) {
    if (!df || !df->fp || offset > df->logical_size ||
        size > df->logical_size - offset || (size > 0 && !dest)) {
        return 0;
    }
    if (size == 0) return 1;

    if (!df->is_ciso) {
        return disc_host_read(df, offset, dest, size);
    }
    return acgc_ciso_read(
        &df->ciso,
        offset,
        size,
        dest,
        disc_host_read,
        df
    ) == ACGC_DISC_OK;
}

/* ---- FST path table builder ---- */
static int append_fst_file(
    void* context,
    const char* path,
    uint32_t offset,
    uint32_t size
) {
    (void)context;

    if (g_fst_file_count >= MAX_FST_FILES ||
        path == NULL || strlen(path) >= sizeof(g_fst_files[0].path)) {
        return 0;
    }

    strncpy(g_fst_files[g_fst_file_count].path, path,
            sizeof(g_fst_files[g_fst_file_count].path) - 1);
    g_fst_files[g_fst_file_count].path[
        sizeof(g_fst_files[g_fst_file_count].path) - 1] = '\0';
    g_fst_files[g_fst_file_count].disc_offset = offset;
    g_fst_files[g_fst_file_count].file_size = size;
    g_fst_file_count++;
    return 1;
}

static int disc_reader_read(
    void* context,
    uint32_t offset,
    void* destination,
    size_t size
) {
    if (size > UINT32_MAX) return 0;
    return disc_read((DiscFile*)context, (u32)offset, destination, (u32)size);
}

static int build_fst_table(const AcgcDiscReader* reader, const AcgcGcmInfo* info) {
    AcgcDiscStatus status;

    g_fst_file_count = 0;
    status = acgc_fst_visit(reader, info, append_fst_file, NULL);
    if (status != ACGC_DISC_OK) {
        if (g_pc_verbose) {
            printf("[PC] FST parse failed: %s\n",
                   acgc_disc_status_string(status));
        }
        g_fst_file_count = 0;
        return 0;
    }

    if (g_pc_verbose) {
        printf("[PC] FST: %d files indexed\n", g_fst_file_count);
        for (int fi = 0; fi < g_fst_file_count; fi++)
            printf("[PC] FST[%d]: %s (%u bytes @ 0x%X)\n", fi,
                   g_fst_files[fi].path, g_fst_files[fi].file_size,
                   g_fst_files[fi].disc_offset);
    }
    return 1;
}

/* ---- disc image search ---- */
static int str_ends_ci(const char* s, const char* suffix) {
    size_t sl = strlen(s), el = strlen(suffix);
    const char* p;
    if (sl < el) return 0;
    p = s + sl - el;
    while (*suffix) {
        if (tolower((unsigned char)*p) != tolower((unsigned char)*suffix))
            return 0;
        p++;
        suffix++;
    }
    return 1;
}

static int find_disc_image(char* out_path, int out_sz) {
    static const char* dirs[] = { ".", "orig", "rom", NULL };
    int d;

    for (d = 0; dirs[d]; d++) {
        DIR* dp = opendir(dirs[d]);
        struct dirent* ent;
        if (!dp) continue;
        while ((ent = readdir(dp)) != NULL) {
            if (str_ends_ci(ent->d_name, ".ciso") ||
                str_ends_ci(ent->d_name, ".iso")  ||
                str_ends_ci(ent->d_name, ".gcm")) {
                if (strcmp(dirs[d], ".") == 0)
                    snprintf(out_path, out_sz, "%s", ent->d_name);
                else
                    snprintf(out_path, out_sz, "%s/%s", dirs[d], ent->d_name);
                closedir(dp);
                return 1;
            }
        }
        closedir(dp);
    }
    return 0;
}

/* ---- public API ---- */

int pc_disc_init(void) {
    char path[512];
    AcgcDiscReader reader;
    AcgcGcmInfo gcm;
    AcgcDiscStatus status;
    uint32_t portable_dol_size;

    if (g_disc_open) return 1;
    if (!find_disc_image(path, sizeof(path))) return 0;
    if (!disc_open(&g_disc, path)) return 0;

    reader.context = &g_disc;
    reader.size = g_disc.logical_size;
    reader.read = disc_reader_read;

    status = acgc_gcm_parse(&reader, &gcm);
    if (status != ACGC_DISC_OK) {
        if (g_pc_verbose) printf("[PC] %s: not a valid GC disc image\n", path);
        disc_close(&g_disc);
        return 0;
    }

    {
        u8 id[7];
        if (!disc_read(&g_disc, 0, id, 6)) {
            disc_close(&g_disc);
            return 0;
        }
        id[6] = '\0';
        if (g_pc_verbose) printf("[PC] Disc image: %s (%s, %s)\n",
            path, g_disc.is_ciso ? "CISO" : "ISO/GCM", id);
    }

    /* cache DOL info */
    g_dol_offset = (u32)gcm.dol_offset;
    status = acgc_dol_get_size(&reader, gcm.dol_offset, &portable_dol_size);
    if (status != ACGC_DISC_OK) {
        if (g_pc_verbose) {
            printf("[PC] DOL parse failed: %s\n",
                   acgc_disc_status_string(status));
        }
        disc_close(&g_disc);
        return 0;
    }
    g_dol_size = (u32)portable_dol_size;

    /* build FST lookup table */
    if (!build_fst_table(&reader, &gcm)) {
        disc_close(&g_disc);
        return 0;
    }

    g_disc_open = 1;
    return 1;
}

int pc_disc_is_open(void) {
    return g_disc_open;
}

int pc_disc_find_file(const char* path, u32* disc_offset, u32* file_size) {
    int i;
    if (!g_disc_open || !path || !disc_offset || !file_size) return 0;

    /* strip leading slash */
    if (path[0] == '/') path++;

    for (i = 0; i < g_fst_file_count; i++) {
        if (strcmp(g_fst_files[i].path, path) == 0) {
            *disc_offset = g_fst_files[i].disc_offset;
            *file_size = g_fst_files[i].file_size;
            return 1;
        }
    }
    return 0;
}

int pc_disc_read(u32 offset, void* dest, u32 size) {
    if (!g_disc_open) return 0;
    return disc_read(&g_disc, offset, dest, size);
}

u8* pc_disc_extract_dol(void) {
    u8* buf;
    if (!g_disc_open) return NULL;
    buf = (u8*)malloc(g_dol_size);
    if (!buf) return NULL;
    if (!disc_read(&g_disc, g_dol_offset, buf, g_dol_size)) {
        free(buf);
        return NULL;
    }
    if (g_pc_verbose)
        printf("[PC] DOL: %u bytes (offset 0x%X)\n", g_dol_size, g_dol_offset);
    return buf;
}

u8* pc_disc_extract_rel(void) {
    u32 off, sz;
    uint8_t* data = NULL;
    uint32_t data_size = 0;
    AcgcRelFormat format = ACGC_REL_RAW;
    AcgcRelLimits limits = {
        ACGC_DISC_DEFAULT_REL_MAX_INPUT_SIZE,
        ACGC_DISC_DEFAULT_REL_MAX_OUTPUT_SIZE
    };
    AcgcDiscReader reader;
    AcgcDiscStatus status;

    if (!pc_disc_find_file("foresta.rel.szs", &off, &sz)) {
        if (g_pc_verbose) printf("[PC] foresta.rel.szs not found in disc FST\n");
        return NULL;
    }

    reader.context = &g_disc;
    reader.size = g_disc.logical_size;
    reader.read = disc_reader_read;
    status = acgc_rel_extract(
        &reader,
        (uint32_t)off,
        (uint32_t)sz,
        &limits,
        &data,
        &data_size,
        &format
    );
    if (status != ACGC_DISC_OK) {
        if (g_pc_verbose) {
            printf("[PC] REL extraction failed: %s\n",
                   acgc_disc_status_string(status));
        }
        return NULL;
    }

    if (data_size == 0) {
        if (g_pc_verbose) printf("[PC] REL extraction produced an empty REL\n");
        free(data);
        return NULL;
    }
    if (g_pc_verbose) {
        if (format == ACGC_REL_YAZ0)
            printf("[PC] REL: %u bytes (Yaz0: %u -> %u)\n", data_size, sz, data_size);
        else
            printf("[PC] REL: %u bytes (raw)\n", data_size);
    }
    return (u8*)data;
}

void pc_disc_shutdown(void) {
    if (g_disc_open) {
        disc_close(&g_disc);
        g_disc_open = 0;
        g_fst_file_count = 0;
    }
}

#endif /* TARGET_PC */
