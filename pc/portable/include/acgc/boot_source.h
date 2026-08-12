#ifndef ACGC_PORTABLE_BOOT_SOURCE_H
#define ACGC_PORTABLE_BOOT_SOURCE_H

#include "acgc/disc.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The GameCube boot header stores game/company ID, disc number, and version. */
#define ACGC_BOOT_SOURCE_REVISION_SIZE UINT32_C(8)
#define ACGC_BOOT_SOURCE_MAX_DOL_SIZE \
    (UINT32_C(16) * UINT32_C(1024) * UINT32_C(1024))
#define ACGC_BOOT_SOURCE_DEFAULT_MAX_DOL_SIZE ACGC_BOOT_SOURCE_MAX_DOL_SIZE

typedef enum AcgcBootSourceStatus {
    ACGC_BOOT_SOURCE_OK = 0,
    ACGC_BOOT_SOURCE_INVALID_ARGUMENT,
    ACGC_BOOT_SOURCE_UNSUPPORTED_REVISION,
    ACGC_BOOT_SOURCE_MISSING_REL,
    ACGC_BOOT_SOURCE_DUPLICATE_REL,
    ACGC_BOOT_SOURCE_DOL_LIMIT_EXCEEDED,
    ACGC_BOOT_SOURCE_REL_INPUT_LIMIT_EXCEEDED,
    ACGC_BOOT_SOURCE_REL_OUTPUT_LIMIT_EXCEEDED,
    ACGC_BOOT_SOURCE_REL_DECODE_FAILED,
    ACGC_BOOT_SOURCE_TRUNCATED_INPUT,
    ACGC_BOOT_SOURCE_READ_FAILED,
    ACGC_BOOT_SOURCE_INVALID_HEADER,
    ACGC_BOOT_SOURCE_INVALID_RANGE,
    ACGC_BOOT_SOURCE_LIMIT_EXCEEDED,
    ACGC_BOOT_SOURCE_CALLBACK_FAILED,
    ACGC_BOOT_SOURCE_ALLOCATION_FAILED,
    ACGC_BOOT_SOURCE_EMPTY_REL
} AcgcBootSourceStatus;

/*
 * These caps contain no host pointers. A zero cap is valid and means that the
 * corresponding image cannot be loaded. Callers may lower the caps; values
 * above the explicit DOL and existing REL maxima are reduced to those maxima.
 * NULL selects the defaults documented below.
 */
typedef struct AcgcBootSourceLimits {
    uint32_t max_dol_size;
    AcgcRelLimits rel;
} AcgcBootSourceLimits;

/*
 * Fixed-width in-memory metadata for the one supported boot revision. It
 * contains no pointers or host allocation state, but its multi-byte fields use
 * host byte order and its C layout is not a wire or guest serialization
 * contract. Use an explicit codec when crossing either boundary. revision is
 * exactly eight bytes and is not a C string.
 */
typedef struct AcgcBootSourceManifest {
    uint8_t revision[ACGC_BOOT_SOURCE_REVISION_SIZE];
    uint32_t dol_offset;
    uint32_t dol_size;
    uint32_t fst_file_count;
    uint32_t rel_input_offset;
    uint32_t rel_input_size;
} AcgcBootSourceManifest;

/*
 * Host-owned preparation state. The buffers are malloc-owned by this object
 * and must not be serialized as guest or wire metadata. dol_data contains the
 * validated DOL byte span; rel_data contains non-empty raw REL bytes or
 * non-empty decoded Yaz0 output.
 */
typedef struct AcgcBootSourceImages {
    AcgcBootSourceManifest manifest;
    uint8_t* dol_data;
    uint8_t* rel_data;
    uint32_t rel_size;
    AcgcRelFormat rel_format;
} AcgcBootSourceImages;

/* Fill limits with the explicit DOL cap and existing bounded REL defaults. */
void acgc_boot_source_limits_default(AcgcBootSourceLimits* limits);

/*
 * Inspect one reader synchronously. The reader and its context remain owned by
 * the caller and must stay valid until this function returns; neither is
 * retained. The output is zeroed before work begins and remains zeroed on any
 * failure. FST callbacks are internal and are not retained after return.
 */
AcgcBootSourceStatus acgc_boot_source_inspect(
    const AcgcDiscReader* reader,
    AcgcBootSourceManifest* manifest
);

/*
 * Inspect and synchronously prepare both boot images in memory. The output
 * must be zero-initialized before first use or disposed before reuse. It is
 * zeroed on failure, and any allocation made during the call is released
 * before returning. The reader/context must remain valid until return.
 * limits may be NULL to select acgc_boot_source_limits_default(). No bytes are
 * written to disk.
 */
AcgcBootSourceStatus acgc_boot_source_prepare(
    const AcgcDiscReader* reader,
    const AcgcBootSourceLimits* limits,
    AcgcBootSourceImages* images
);

/* Safe for NULL, zeroed, or partially populated preparation state. */
void acgc_boot_source_dispose(AcgcBootSourceImages* images);

const char* acgc_boot_source_status_string(AcgcBootSourceStatus status);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(
    offsetof(AcgcBootSourceManifest, revision) == 0,
    "boot manifest revision offset changed"
);
static_assert(
    offsetof(AcgcBootSourceManifest, dol_offset) == 8,
    "boot manifest DOL offset changed"
);
static_assert(
    offsetof(AcgcBootSourceManifest, dol_size) == 12,
    "boot manifest DOL size changed"
);
static_assert(
    offsetof(AcgcBootSourceManifest, fst_file_count) == 16,
    "boot manifest FST count changed"
);
static_assert(
    offsetof(AcgcBootSourceManifest, rel_input_offset) == 20,
    "boot manifest REL offset changed"
);
static_assert(
    offsetof(AcgcBootSourceManifest, rel_input_size) == 24,
    "boot manifest REL size changed"
);
static_assert(
    sizeof(AcgcBootSourceManifest) == 28,
    "boot manifest layout changed"
);
#else
_Static_assert(
    offsetof(AcgcBootSourceManifest, revision) == 0,
    "boot manifest revision offset changed"
);
_Static_assert(
    offsetof(AcgcBootSourceManifest, dol_offset) == 8,
    "boot manifest DOL offset changed"
);
_Static_assert(
    offsetof(AcgcBootSourceManifest, dol_size) == 12,
    "boot manifest DOL size changed"
);
_Static_assert(
    offsetof(AcgcBootSourceManifest, fst_file_count) == 16,
    "boot manifest FST count changed"
);
_Static_assert(
    offsetof(AcgcBootSourceManifest, rel_input_offset) == 20,
    "boot manifest REL offset changed"
);
_Static_assert(
    offsetof(AcgcBootSourceManifest, rel_input_size) == 24,
    "boot manifest REL size changed"
);
_Static_assert(
    sizeof(AcgcBootSourceManifest) == 28,
    "boot manifest layout changed"
);
#endif

#endif /* ACGC_PORTABLE_BOOT_SOURCE_H */
