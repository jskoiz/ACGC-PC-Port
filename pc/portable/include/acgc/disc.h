#ifndef ACGC_PORTABLE_DISC_H
#define ACGC_PORTABLE_DISC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A bounded logical-disc reader. The callback must return non-zero only when
 * all requested bytes were read. The size field is part of the contract: the
 * parser never asks the callback for bytes outside [0, size).
 */
typedef int (*AcgcDiscReadFn)(
    void* context,
    uint32_t offset,
    void* destination,
    size_t size
);

typedef struct AcgcDiscReader {
    void* context;
    uint32_t size;
    AcgcDiscReadFn read;
} AcgcDiscReader;

typedef enum AcgcDiscStatus {
    ACGC_DISC_OK = 0,
    ACGC_DISC_INVALID_ARGUMENT,
    ACGC_DISC_TRUNCATED_INPUT,
    ACGC_DISC_READ_FAILED,
    ACGC_DISC_INVALID_HEADER,
    ACGC_DISC_INVALID_RANGE,
    ACGC_DISC_LIMIT_EXCEEDED,
    ACGC_DISC_CALLBACK_FAILED,
    ACGC_DISC_ALLOCATION_FAILED,
    ACGC_DISC_EMPTY_INPUT
} AcgcDiscStatus;

typedef struct AcgcGcmInfo {
    uint32_t dol_offset;
    uint32_t fst_offset;
    uint32_t fst_size;
    uint32_t fst_max_size;
} AcgcGcmInfo;

typedef int (*AcgcFstFileCallback)(
    void* context,
    const char* path,
    uint32_t offset,
    uint32_t size
);

typedef enum AcgcRelFormat {
    ACGC_REL_RAW = 0,
    ACGC_REL_YAZ0
} AcgcRelFormat;

typedef struct AcgcRelLimits {
    uint32_t max_input_size;
    uint32_t max_output_size;
} AcgcRelLimits;

#define ACGC_DISC_GCM_HEADER_SIZE UINT32_C(0x430)
#define ACGC_DISC_DOL_HEADER_SIZE UINT32_C(0xE4)
#define ACGC_DISC_FST_ENTRY_SIZE UINT32_C(12)
#define ACGC_DISC_MAX_FST_ENTRIES UINT32_C(1048576)
#define ACGC_DISC_MAX_FST_NAME_SIZE UINT32_C(256)
#define ACGC_DISC_MAX_FST_PATH_SIZE UINT32_C(1024)
#define ACGC_DISC_DEFAULT_REL_MAX_INPUT_SIZE \
    (UINT32_C(64) * UINT32_C(1024) * UINT32_C(1024))
#define ACGC_DISC_DEFAULT_REL_MAX_OUTPUT_SIZE \
    (UINT32_C(64) * UINT32_C(1024) * UINT32_C(1024))

/* Parse the checked portion of a GameCube disc header. */
AcgcDiscStatus acgc_gcm_parse(
    const AcgcDiscReader* reader,
    AcgcGcmInfo* info
);

/* Calculate the byte span of a DOL, including its header and sections. */
AcgcDiscStatus acgc_dol_get_size(
    const AcgcDiscReader* reader,
    uint32_t dol_offset,
    uint32_t* dol_size
);

/* Visit each bounded file entry in the GCM FST without allocating the table. */
AcgcDiscStatus acgc_fst_visit(
    const AcgcDiscReader* reader,
    const AcgcGcmInfo* info,
    AcgcFstFileCallback callback,
    void* context
);

/*
 * Read a raw REL or decode a Yaz0 REL from one checked FST range. The caller
 * owns the returned buffer and must release it with free(). A null limits
 * pointer selects the defaults above. format may be null.
 */
AcgcDiscStatus acgc_rel_extract(
    const AcgcDiscReader* reader,
    uint32_t offset,
    uint32_t size,
    const AcgcRelLimits* limits,
    uint8_t** output_data,
    uint32_t* output_size,
    AcgcRelFormat* format
);

const char* acgc_disc_status_string(AcgcDiscStatus status);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_PORTABLE_DISC_H */
