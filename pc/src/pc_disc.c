/* Read files from GC disc images (CISO/ISO/GCM)
 * Used by pc_assets.c for DOL+REL extraction and pc_dvd.c for runtime file reads. */
#ifdef TARGET_PC
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include "types.h"
#include "acgc/bytes.h"
#include "acgc/disc.h"
#include "pc_disc.h"

extern int g_pc_verbose;

/* ---- CISO format ---- */
#define CISO_HDR_SIZE 0x8000
#define CISO_MAGIC    0x4F534943 /* "CISO" as LE u32 */
#define CISO_MAP_OFF  8

typedef struct {
    FILE* fp;
    int is_ciso;
    u32 block_size;
    int num_blocks;
    int* block_phys; /* logical block -> physical block, -1 = absent */
    u32 logical_size;
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
static int disc_open(DiscFile* df, const char* path) {
    u8 hdr[CISO_HDR_SIZE];

    memset(df, 0, sizeof(*df));
    df->fp = fopen(path, "rb");
    if (!df->fp) return 0;

    /* try CISO */
    if (fread(hdr, 1, CISO_HDR_SIZE, df->fp) == CISO_HDR_SIZE &&
        acgc_load_le32(hdr) == CISO_MAGIC) {
        df->block_size = acgc_load_le32(hdr + 4);
        if (df->block_size > 0) {
            int i, phys = 0;
            df->num_blocks = CISO_HDR_SIZE - CISO_MAP_OFF;
            if (df->block_size > UINT32_MAX / (u32)df->num_blocks) {
                fclose(df->fp);
                memset(df, 0, sizeof(*df));
                return 0;
            }
            df->block_phys = (int*)malloc(df->num_blocks * sizeof(int));
            if (!df->block_phys) {
                fclose(df->fp);
                memset(df, 0, sizeof(*df));
                return 0;
            }
            for (i = 0; i < df->num_blocks; i++)
                df->block_phys[i] = hdr[CISO_MAP_OFF + i] ? phys++ : -1;
            df->logical_size = df->block_size * (u32)df->num_blocks;
            df->is_ciso = 1;
            return 1;
        }
    }

    /* plain ISO/GCM */
    {
        long file_size;

        if (fseek(df->fp, 0, SEEK_END) != 0) {
            fclose(df->fp);
            memset(df, 0, sizeof(*df));
            return 0;
        }
        file_size = ftell(df->fp);
        if (file_size < 0 || (uint64_t)file_size > UINT32_MAX ||
            fseek(df->fp, 0, SEEK_SET) != 0) {
            fclose(df->fp);
            memset(df, 0, sizeof(*df));
            return 0;
        }
        df->logical_size = (u32)file_size;
    }
    df->is_ciso = 0;
    return 1;
}

static void disc_close(DiscFile* df) {
    if (df->fp) fclose(df->fp);
    if (df->block_phys) free(df->block_phys);
    memset(df, 0, sizeof(*df));
}

static int disc_read(DiscFile* df, u32 offset, void* dest, u32 size) {
    if (!df || !df->fp || offset > df->logical_size ||
        size > df->logical_size - offset || (size > 0 && !dest)) {
        return 0;
    }
    if (size == 0) return 1;

    if (!df->is_ciso) {
        if (fseek(df->fp, (long)offset, SEEK_SET) != 0) return 0;
        return (u32)fread(dest, 1, size, df->fp) == size;
    }

    {
        u8* out = (u8*)dest;
        while (size > 0) {
            u32 bi = offset / df->block_size;
            u32 bo = offset % df->block_size;
            u32 chunk = df->block_size - bo;
            if (chunk > size) chunk = size;

            if ((int)bi >= df->num_blocks || df->block_phys[bi] < 0) {
                memset(out, 0, chunk);
            } else {
                u32 phys = CISO_HDR_SIZE +
                    (u32)df->block_phys[bi] * df->block_size + bo;
                if (fseek(df->fp, (long)phys, SEEK_SET) != 0) return 0;
                if ((u32)fread(out, 1, chunk, df->fp) != chunk) return 0;
            }

            out += chunk;
            offset += chunk;
            size -= chunk;
        }
    }
    return 1;
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
