#ifndef ACGC_MACOS_HOST_H
#define ACGC_MACOS_HOST_H

#include "acgc/disc.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACGC_MACOS_HOST_BUNDLE_IDENTIFIER "org.acgc.modernport.host"
#define ACGC_MACOS_HOST_PATH_CAPACITY 1024
#define ACGC_MACOS_HOST_ERROR_CAPACITY 256
#define ACGC_MACOS_HOST_MAX_VERIFY_SECONDS 60.0

typedef struct AcgcMacosHostOptions {
    const char* disc_path;
    double verify_seconds;
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
    ACGC_MACOS_HOST_ID_READ_FAILED,
    ACGC_MACOS_HOST_UNSUPPORTED_DISC_ID,
    ACGC_MACOS_HOST_GCM_INVALID,
    ACGC_MACOS_HOST_DOL_INVALID,
    ACGC_MACOS_HOST_FST_INVALID,
    ACGC_MACOS_HOST_SELF_TEST_FAILED
} AcgcMacosHostStatus;

typedef struct AcgcMacosDiscReport {
    AcgcMacosHostStatus status;
    AcgcDiscStatus portable_status;
    char input_path[ACGC_MACOS_HOST_PATH_CAPACITY];
    char resolved_path[ACGC_MACOS_HOST_PATH_CAPACITY];
    char disc_id[7];
    char error[ACGC_MACOS_HOST_ERROR_CAPACITY];
    uint32_t file_size;
    uint32_t dol_size;
    uint32_t fst_file_count;
    AcgcGcmInfo gcm;
} AcgcMacosDiscReport;

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

/* Run deterministic synthetic header/FST/DOL and option-parser checks. */
int acgc_macos_host_run_self_test(void);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_MACOS_HOST_H */
