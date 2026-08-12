#ifndef ACGC_MACOS_HOST_H
#define ACGC_MACOS_HOST_H

#include "acgc/boot_source.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACGC_MACOS_HOST_BUNDLE_IDENTIFIER "org.acgc.modernport.host"
#define ACGC_MACOS_HOST_PATH_CAPACITY 1024
#define ACGC_MACOS_HOST_ERROR_CAPACITY 256
#define ACGC_MACOS_HOST_MAX_VERIFY_SECONDS 60.0
#define ACGC_MACOS_HOST_MAX_VERIFY_FRAMES 600U

typedef struct AcgcMacosHostOptions {
    const char* disc_path;
    double verify_seconds;
    uint32_t verify_frames;
    int self_test;
    int headless;
    int show_help;
} AcgcMacosHostOptions;

typedef struct AcgcMacosHostPaths {
    char application_support[ACGC_MACOS_HOST_PATH_CAPACITY];
    char caches[ACGC_MACOS_HOST_PATH_CAPACITY];
} AcgcMacosHostPaths;

typedef enum AcgcMacosHostStatus {
    ACGC_MACOS_HOST_OK = 0,
    ACGC_MACOS_HOST_INVALID_ARGUMENT,
    ACGC_MACOS_HOST_PATH_TOO_LONG,
    ACGC_MACOS_HOST_OPEN_FAILED,
    ACGC_MACOS_HOST_STAT_FAILED,
    ACGC_MACOS_HOST_UNSUPPORTED_FILE,
    ACGC_MACOS_HOST_BOOT_SOURCE_FAILED,
    ACGC_MACOS_HOST_SELF_TEST_FAILED
} AcgcMacosHostStatus;

/*
 * Host-owned paths and fixed-width boot-source metadata only. The DOL/REL
 * buffers prepared by the portable facade never cross this API boundary.
 */
typedef struct AcgcMacosDiscReport {
    AcgcMacosHostStatus status;
    AcgcBootSourceStatus boot_source_status;
    char input_path[ACGC_MACOS_HOST_PATH_CAPACITY];
    char resolved_path[ACGC_MACOS_HOST_PATH_CAPACITY];
    uint8_t revision[ACGC_BOOT_SOURCE_REVISION_SIZE];
    char error[ACGC_MACOS_HOST_ERROR_CAPACITY];
    uint32_t file_size;
    uint32_t dol_size;
    uint32_t fst_file_count;
    uint32_t rel_input_size;
    uint32_t rel_output_size;
    AcgcRelFormat rel_format;
} AcgcMacosDiscReport;

/*
 * Host-owned metadata plus prepared DOL/REL buffers. The buffers remain live
 * until the caller transfers them to acgc_game_runtime_create() or calls the
 * matching dispose function.
 */
typedef struct AcgcMacosPreparedDisc {
    AcgcMacosDiscReport report;
    AcgcBootSourceImages images;
} AcgcMacosPreparedDisc;

/* Parse only explicit host options. No filesystem search is performed. */
int acgc_macos_host_parse_options(
    int argc,
    const char* const* argv,
    AcgcMacosHostOptions* options,
    char* error,
    size_t error_capacity
);

const char* acgc_macos_host_usage(void);

/* Validate one explicitly supplied, read-only ISO/GCM path. */
AcgcMacosHostStatus acgc_macos_host_validate_disc(
    const char* path,
    AcgcMacosDiscReport* report
);

/* Prepare one explicit disc and retain its boot images for runtime startup. */
AcgcMacosHostStatus acgc_macos_host_prepare_disc(
    const char* path,
    AcgcMacosPreparedDisc* prepared
);

/* Dispose retained boot images and clear the prepared-disc record. */
void acgc_macos_host_dispose_prepared_disc(
    AcgcMacosPreparedDisc* prepared
);

const char* acgc_macos_host_status_string(AcgcMacosHostStatus status);

/* Resolve and create only the scoped Application Support and Caches roots. */
int acgc_macos_host_prepare_paths(
    AcgcMacosHostPaths* paths,
    char* error,
    size_t error_capacity
);

void acgc_macos_host_format_status(
    const AcgcMacosHostPaths* paths,
    const AcgcMacosDiscReport* report,
    char* output,
    size_t output_capacity
);

/* Run deterministic synthetic boot-source, metadata, and option-parser checks. */
int acgc_macos_host_run_self_test(void);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_MACOS_HOST_H */
