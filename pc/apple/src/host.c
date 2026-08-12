#include "acgc/macos_host.h"

#include "acgc/bytes.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ACGC_MACOS_HOST_SUPPORTED_DISC_ID "GAFE01"
#define ACGC_MACOS_HOST_DISC_ID_SIZE 6
static const char ACGC_MACOS_HOST_VERIFY_SECONDS_PREFIX[] = "--verify-seconds=";

typedef struct AcgcMacosFileReader {
    int fd;
    uint32_t size;
} AcgcMacosFileReader;

typedef struct AcgcMacosMemoryReader {
    const uint8_t* bytes;
    uint32_t size;
} AcgcMacosMemoryReader;

static void set_error(char* error, size_t capacity, const char* format, ...) {
    va_list args;

    if (error == NULL || capacity == 0) {
        return;
    }
    va_start(args, format);
    (void)vsnprintf(error, capacity, format, args);
    va_end(args);
}

static int copy_string(char* destination, size_t capacity, const char* source) {
    size_t length;

    if (destination == NULL || capacity == 0 || source == NULL) {
        return 0;
    }
    length = strlen(source);
    if (length >= capacity) {
        destination[0] = '\0';
        return 0;
    }
    memcpy(destination, source, length + 1);
    return 1;
}

static void reset_report(AcgcMacosDiscReport* report) {
    if (report == NULL) {
        return;
    }
    memset(report, 0, sizeof(*report));
    report->status = ACGC_MACOS_HOST_INVALID_ARGUMENT;
    report->portable_status = ACGC_DISC_INVALID_ARGUMENT;
}

static AcgcMacosHostStatus fail_report(
    AcgcMacosDiscReport* report,
    AcgcMacosHostStatus status,
    AcgcDiscStatus portable_status,
    const char* format,
    ...
) {
    va_list args;

    if (report != NULL) {
        report->status = status;
        report->portable_status = portable_status;
        va_start(args, format);
        (void)vsnprintf(report->error, sizeof(report->error), format, args);
        va_end(args);
    }
    return status;
}

static int file_reader_read(
    void* context,
    uint32_t offset,
    void* destination,
    size_t size
) {
    AcgcMacosFileReader* reader = (AcgcMacosFileReader*)context;
    uint8_t* bytes = (uint8_t*)destination;
    size_t completed = 0;

    if (reader == NULL || reader->fd < 0 ||
        (size > 0 && destination == NULL) ||
        offset > reader->size || size > reader->size - offset) {
        return 0;
    }
    while (completed < size) {
        ssize_t result = pread(
            reader->fd,
            bytes + completed,
            size - completed,
            (off_t)offset + (off_t)completed
        );
        if (result <= 0) {
            return 0;
        }
        completed += (size_t)result;
    }
    return 1;
}

static int memory_reader_read(
    void* context,
    uint32_t offset,
    void* destination,
    size_t size
) {
    AcgcMacosMemoryReader* reader = (AcgcMacosMemoryReader*)context;

    if (reader == NULL || offset > reader->size ||
        size > reader->size - offset || (size > 0 && destination == NULL)) {
        return 0;
    }
    if (size > 0) {
        memcpy(destination, reader->bytes + offset, size);
    }
    return 1;
}

static int count_fst_file(
    void* context,
    const char* path,
    uint32_t offset,
    uint32_t size
) {
    uint32_t* count = (uint32_t*)context;

    (void)path;
    (void)offset;
    (void)size;
    if (count == NULL || *count == UINT32_MAX) {
        return 0;
    }
    *count += 1;
    return 1;
}

static AcgcMacosHostStatus validate_reader(
    const char* display_path,
    const char* resolved_path,
    const AcgcDiscReader* reader,
    AcgcMacosDiscReport* report
) {
    uint8_t id[ACGC_MACOS_HOST_DISC_ID_SIZE];
    AcgcGcmInfo gcm;
    uint32_t dol_size = 0;
    uint32_t fst_file_count = 0;
    AcgcDiscStatus portable_status;

    reset_report(report);
    if (report == NULL || reader == NULL || reader->read == NULL ||
        display_path == NULL) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_INVALID_ARGUMENT,
            ACGC_DISC_INVALID_ARGUMENT,
            "a disc path and bounded reader are required"
        );
    }
    if (!copy_string(report->input_path, sizeof(report->input_path), display_path)) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_PATH_TOO_LONG,
            ACGC_DISC_INVALID_ARGUMENT,
            "disc path exceeds %u bytes",
            (unsigned)(sizeof(report->input_path) - 1)
        );
    }
    if (resolved_path != NULL &&
        !copy_string(report->resolved_path, sizeof(report->resolved_path), resolved_path)) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_PATH_TOO_LONG,
            ACGC_DISC_INVALID_ARGUMENT,
            "resolved disc path exceeds %u bytes",
            (unsigned)(sizeof(report->resolved_path) - 1)
        );
    }
    report->file_size = reader->size;

    if (reader->size < ACGC_MACOS_HOST_DISC_ID_SIZE ||
        !reader->read(reader->context, 0, id, sizeof(id))) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_ID_READ_FAILED,
            ACGC_DISC_TRUNCATED_INPUT,
            "could not read the six-byte GameCube disc ID"
        );
    }
    memcpy(report->disc_id, id, sizeof(id));
    report->disc_id[sizeof(id)] = '\0';
    if (memcmp(id, ACGC_MACOS_HOST_SUPPORTED_DISC_ID, sizeof(id)) != 0) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_UNSUPPORTED_DISC_ID,
            ACGC_DISC_OK,
            "unsupported disc ID '%s'; expected exact '%s'",
            report->disc_id,
            ACGC_MACOS_HOST_SUPPORTED_DISC_ID
        );
    }

    portable_status = acgc_gcm_parse(reader, &gcm);
    report->portable_status = portable_status;
    if (portable_status != ACGC_DISC_OK) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_GCM_INVALID,
            portable_status,
            "bounded GCM header validation failed: %s",
            acgc_disc_status_string(portable_status)
        );
    }

    portable_status = acgc_dol_get_size(reader, gcm.dol_offset, &dol_size);
    report->portable_status = portable_status;
    if (portable_status != ACGC_DISC_OK) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_DOL_INVALID,
            portable_status,
            "bounded DOL validation failed: %s",
            acgc_disc_status_string(portable_status)
        );
    }

    portable_status = acgc_fst_visit(
        reader,
        &gcm,
        count_fst_file,
        &fst_file_count
    );
    report->portable_status = portable_status;
    if (portable_status != ACGC_DISC_OK) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_FST_INVALID,
            portable_status,
            "bounded FST validation failed: %s",
            acgc_disc_status_string(portable_status)
        );
    }

    report->status = ACGC_MACOS_HOST_OK;
    report->portable_status = ACGC_DISC_OK;
    report->gcm = gcm;
    report->dol_size = dol_size;
    report->fst_file_count = fst_file_count;
    return ACGC_MACOS_HOST_OK;
}

int acgc_macos_host_parse_options(
    int argc,
    const char* const* argv,
    AcgcMacosHostOptions* options,
    char* error,
    size_t error_capacity
) {
    int i;

    if (options == NULL || argc < 0 || (argc > 0 && argv == NULL)) {
        set_error(error, error_capacity, "invalid option-parser arguments");
        return 0;
    }
    memset(options, 0, sizeof(*options));
    for (i = 1; i < argc; i++) {
        const char* argument = argv[i];
        if (argument == NULL) {
            set_error(error, error_capacity, "argument %d is null", i);
            return 0;
        }
        if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
            options->show_help = 1;
        } else if (strcmp(argument, "--self-test") == 0) {
            options->self_test = 1;
        } else if (strcmp(argument, "--headless") == 0) {
            options->headless = 1;
        } else if (strcmp(argument, "--disc") == 0) {
            if (i + 1 >= argc || argv[i + 1] == NULL || argv[i + 1][0] == '\0') {
                set_error(error, error_capacity, "--disc requires an explicit path");
                return 0;
            }
            options->disc_path = argv[++i];
        } else if (strncmp(argument, "--disc=", 7) == 0) {
            if (argument[7] == '\0') {
                set_error(error, error_capacity, "--disc= requires an explicit path");
                return 0;
            }
            options->disc_path = argument + 7;
        } else if (strcmp(argument, "--verify-seconds") == 0 ||
                   strncmp(
                       argument,
                       ACGC_MACOS_HOST_VERIFY_SECONDS_PREFIX,
                       sizeof(ACGC_MACOS_HOST_VERIFY_SECONDS_PREFIX) - 1
                   ) == 0) {
            const char* value = strncmp(
                    argument,
                    ACGC_MACOS_HOST_VERIFY_SECONDS_PREFIX,
                    sizeof(ACGC_MACOS_HOST_VERIFY_SECONDS_PREFIX) - 1
                ) == 0
                ? argument + sizeof(ACGC_MACOS_HOST_VERIFY_SECONDS_PREFIX) - 1
                : NULL;
            char* end = NULL;
            double seconds;

            if (value == NULL) {
                if (i + 1 >= argc || argv[i + 1] == NULL) {
                    set_error(error, error_capacity, "--verify-seconds requires a positive number");
                    return 0;
                }
                value = argv[++i];
            }
            errno = 0;
            seconds = strtod(value, &end);
            if (errno != 0 || end == value || *end != '\0' ||
                !isfinite(seconds) || seconds <= 0.0 ||
                seconds > ACGC_MACOS_HOST_MAX_VERIFY_SECONDS) {
                set_error(
                    error,
                    error_capacity,
                    "--verify-seconds must be in (0, %.0f]",
                    ACGC_MACOS_HOST_MAX_VERIFY_SECONDS
                );
                return 0;
            }
            options->verify_seconds = seconds;
        } else {
            set_error(error, error_capacity, "unknown option '%s'", argument);
            return 0;
        }
    }
    if (options->self_test && options->headless) {
        set_error(error, error_capacity, "--self-test and --headless are mutually exclusive");
        return 0;
    }
    return 1;
}

const char* acgc_macos_host_usage(void) {
    return
        "Usage: acgc_macos_native_host [--disc PATH] [--verify-seconds N]\n"
        "       acgc_macos_native_host --headless --disc PATH\n"
        "       acgc_macos_native_host --self-test\n"
        "\n"
        "The host accepts one explicit read-only ISO/GCM path and never searches\n"
        "for or embeds proprietary game data. --verify-seconds is bounded to 60s.\n";
}

AcgcMacosHostStatus acgc_macos_host_validate_disc(
    const char* path,
    AcgcMacosDiscReport* report
) {
    AcgcMacosFileReader file_reader;
    AcgcDiscReader reader;
    struct stat file_stat;
    char resolved_path[ACGC_MACOS_HOST_PATH_CAPACITY];
    int flags = O_RDONLY;
    int fd;

    reset_report(report);
    if (path == NULL || path[0] == '\0' || report == NULL) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_INVALID_ARGUMENT,
            ACGC_DISC_INVALID_ARGUMENT,
            "an explicit non-empty disc path is required"
        );
    }
    if (strlen(path) >= ACGC_MACOS_HOST_PATH_CAPACITY) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_PATH_TOO_LONG,
            ACGC_DISC_INVALID_ARGUMENT,
            "disc path exceeds %u bytes",
            ACGC_MACOS_HOST_PATH_CAPACITY - 1
        );
    }
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    fd = open(path, flags);
    if (fd < 0) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_OPEN_FAILED,
            ACGC_DISC_READ_FAILED,
            "could not open '%s' read-only: %s",
            path,
            strerror(errno)
        );
    }
    if (fstat(fd, &file_stat) != 0) {
        int saved_errno = errno;
        close(fd);
        return fail_report(
            report,
            ACGC_MACOS_HOST_STAT_FAILED,
            ACGC_DISC_READ_FAILED,
            "could not stat '%s': %s",
            path,
            strerror(saved_errno)
        );
    }
    if (!S_ISREG(file_stat.st_mode)) {
        close(fd);
        return fail_report(
            report,
            ACGC_MACOS_HOST_UNSUPPORTED_FILE,
            ACGC_DISC_INVALID_ARGUMENT,
            "disc path '%s' is not a regular file",
            path
        );
    }
    if (file_stat.st_size < 0 || (uint64_t)file_stat.st_size > UINT32_MAX) {
        close(fd);
        return fail_report(
            report,
            ACGC_MACOS_HOST_UNSUPPORTED_FILE,
            ACGC_DISC_INVALID_RANGE,
            "disc file size is outside the bounded 32-bit portable-reader range"
        );
    }
    if (realpath(path, resolved_path) == NULL) {
        int saved_errno = errno;
        close(fd);
        return fail_report(
            report,
            ACGC_MACOS_HOST_OPEN_FAILED,
            ACGC_DISC_READ_FAILED,
            "could not resolve disc path '%s': %s",
            path,
            strerror(saved_errno)
        );
    }

    file_reader.fd = fd;
    file_reader.size = (uint32_t)file_stat.st_size;
    reader.context = &file_reader;
    reader.size = file_reader.size;
    reader.read = file_reader_read;
    {
        AcgcMacosHostStatus status = validate_reader(
            path,
            resolved_path,
            &reader,
            report
        );
        close(fd);
        return status;
    }
}

const char* acgc_macos_host_status_string(AcgcMacosHostStatus status) {
    switch (status) {
        case ACGC_MACOS_HOST_OK: return "ok";
        case ACGC_MACOS_HOST_INVALID_ARGUMENT: return "invalid argument";
        case ACGC_MACOS_HOST_PATH_TOO_LONG: return "path too long";
        case ACGC_MACOS_HOST_OPEN_FAILED: return "open failed";
        case ACGC_MACOS_HOST_STAT_FAILED: return "stat failed";
        case ACGC_MACOS_HOST_UNSUPPORTED_FILE: return "unsupported file";
        case ACGC_MACOS_HOST_ID_READ_FAILED: return "disc ID read failed";
        case ACGC_MACOS_HOST_UNSUPPORTED_DISC_ID: return "unsupported disc ID";
        case ACGC_MACOS_HOST_GCM_INVALID: return "invalid GCM header";
        case ACGC_MACOS_HOST_DOL_INVALID: return "invalid DOL";
        case ACGC_MACOS_HOST_FST_INVALID: return "invalid FST";
        case ACGC_MACOS_HOST_SELF_TEST_FAILED: return "self-test failed";
    }
    return "unknown host status";
}

static void append_status(
    char* output,
    size_t capacity,
    size_t* length,
    const char* format,
    ...
) {
    va_list args;
    int written;

    if (output == NULL || length == NULL || capacity == 0 || *length >= capacity) {
        return;
    }
    va_start(args, format);
    written = vsnprintf(output + *length, capacity - *length, format, args);
    va_end(args);
    if (written < 0) {
        output[*length] = '\0';
        return;
    }
    if ((size_t)written >= capacity - *length) {
        *length = capacity - 1;
        output[*length] = '\0';
    } else {
        *length += (size_t)written;
    }
}

void acgc_macos_host_format_status(
    const AcgcMacosHostPaths* paths,
    const AcgcMacosDiscReport* report,
    char* output,
    size_t output_capacity
) {
    size_t length = 0;

    if (output == NULL || output_capacity == 0) {
        return;
    }
    output[0] = '\0';
    append_status(output, output_capacity, &length,
        "ACGC Modern macOS Native Host\n"
        "Native AppKit/Foundation foreground shell\n"
        "Bundle scope: %s\n\n",
        ACGC_MACOS_HOST_BUNDLE_IDENTIFIER);
    append_status(output, output_capacity, &length,
        "Disc policy: explicit --disc only; no image search and no embedded game data.\n");
    if (report == NULL || report->input_path[0] == '\0') {
        append_status(output, output_capacity, &length,
            "Disc validation: not run (supply --disc PATH).\n");
    } else {
        append_status(output, output_capacity, &length,
            "Disc path: %s\n", report->input_path);
        if (report->resolved_path[0] != '\0') {
            append_status(output, output_capacity, &length,
                "Resolved path: %s\n", report->resolved_path);
        }
        append_status(output, output_capacity, &length,
            "Disc ID: %s%s\n",
            report->disc_id[0] != '\0' ? report->disc_id : "<unread>",
            report->status == ACGC_MACOS_HOST_OK ? " (accepted)" : " (rejected)");
        append_status(output, output_capacity, &length,
            "Validation: %s\n", acgc_macos_host_status_string(report->status));
        if (report->status == ACGC_MACOS_HOST_OK) {
            append_status(output, output_capacity, &length,
                "GCM header: valid (DOL 0x%08X, FST 0x%08X + 0x%X)\n"
                "DOL: valid (%u bytes)\n"
                "FST: valid (%u files visited)\n",
                report->gcm.dol_offset,
                report->gcm.fst_offset,
                report->gcm.fst_size,
                report->dol_size,
                report->fst_file_count);
        } else if (report->error[0] != '\0') {
            append_status(output, output_capacity, &length,
                "Reason: %s\n", report->error);
        }
    }
    append_status(output, output_capacity, &length, "\n");
    if (paths != NULL) {
        append_status(output, output_capacity, &length,
            "Application Support: %s\nCaches: %s\n"
            "Only these scoped directories are created; game assets are never written.\n",
            paths->application_support,
            paths->caches);
    }
    append_status(output, output_capacity, &length,
        "\nCapability gates (not implemented):\n"
        "  Rendering: not implemented\n"
        "  Game frame: not implemented\n"
        "  Input: not implemented\n"
        "  Audio: not implemented\n"
        "  Save/load: not implemented\n");
}

static void store_be32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static void store_fst_entry(
    uint8_t* entry,
    uint8_t type,
    uint32_t name_offset,
    uint32_t offset,
    uint32_t size
) {
    entry[0] = type;
    entry[1] = (uint8_t)(name_offset >> 16);
    entry[2] = (uint8_t)(name_offset >> 8);
    entry[3] = (uint8_t)name_offset;
    store_be32(entry + 4, offset);
    store_be32(entry + 8, size);
}

static void make_self_test_image(uint8_t* image, size_t image_size) {
    uint8_t* fst;
    uint8_t* dol;

    memset(image, 0, image_size);
    memcpy(image, ACGC_MACOS_HOST_SUPPORTED_DISC_ID, 6);
    store_be32(image + 0x1C, UINT32_C(0xC2339F3D));
    store_be32(image + 0x420, UINT32_C(0x500));
    store_be32(image + 0x424, UINT32_C(0x700));
    store_be32(image + 0x428, UINT32_C(0x80));
    store_be32(image + 0x42C, UINT32_C(0x80));

    dol = image + 0x500;
    store_be32(dol + 0x00, UINT32_C(0xE4));
    store_be32(dol + 0x90, UINT32_C(4));
    memcpy(dol + 0xE4, "DOL!", 4);

    fst = image + 0x700;
    store_fst_entry(fst + 0x00, 1, 0, 0, 3);
    store_fst_entry(fst + 0x0C, 1, 1, 0, 3);
    store_fst_entry(fst + 0x18, 0, 5, 0xA00, 3);
    memcpy(fst + 0x24, "\0dir\0file.bin\0", 14);
    memcpy(image + 0xA00, "DAT", 3);
}

static int self_test_options(void) {
    AcgcMacosHostOptions options;
    char error[ACGC_MACOS_HOST_ERROR_CAPACITY];
    const char* valid_argv[] = {
        "host", "--disc", "/tmp/GAFE01.gcm", "--verify-seconds", "0.25"
    };
    const char* equals_argv[] = {
        "host", "--disc=/tmp/GAFE01.gcm", "--verify-seconds=0.5"
    };
    const char* invalid_argv[] = { "host", "--verify-seconds", "61" };

    if (!acgc_macos_host_parse_options(
            (int)(sizeof(valid_argv) / sizeof(valid_argv[0])),
            valid_argv,
            &options,
            error,
            sizeof(error)) ||
        options.disc_path == NULL ||
        strcmp(options.disc_path, "/tmp/GAFE01.gcm") != 0 ||
        options.verify_seconds != 0.25 || options.headless) {
        return 0;
    }
    if (!acgc_macos_host_parse_options(
            (int)(sizeof(equals_argv) / sizeof(equals_argv[0])),
            equals_argv,
            &options,
            error,
            sizeof(error)) ||
        options.headless || options.disc_path == NULL ||
        strcmp(options.disc_path, "/tmp/GAFE01.gcm") != 0 ||
        options.verify_seconds != 0.5) {
        return 0;
    }
    if (acgc_macos_host_parse_options(
            (int)(sizeof(invalid_argv) / sizeof(invalid_argv[0])),
            invalid_argv,
            &options,
            error,
            sizeof(error))) {
        return 0;
    }
    return 1;
}

int acgc_macos_host_run_self_test(void) {
    uint8_t image[0x1000];
    AcgcMacosMemoryReader memory_reader;
    AcgcDiscReader reader;
    AcgcMacosDiscReport report;
    AcgcMacosHostStatus status;

    if (!self_test_options()) {
        fprintf(stderr, "host self-test: option parsing failed\n");
        return 1;
    }
    make_self_test_image(image, sizeof(image));
    memory_reader.bytes = image;
    memory_reader.size = (uint32_t)sizeof(image);
    reader.context = &memory_reader;
    reader.size = memory_reader.size;
    reader.read = memory_reader_read;

    status = validate_reader("<synthetic GAFE01>", "<synthetic>", &reader, &report);
    if (status != ACGC_MACOS_HOST_OK || report.fst_file_count != 1 ||
        report.dol_size != 0xE8 || strcmp(report.disc_id, "GAFE01") != 0) {
        fprintf(stderr, "host self-test: valid GAFE01 image failed\n");
        return 1;
    }
    memcpy(image, "GAFE02", 6);
    status = validate_reader("<synthetic GAFE02>", "<synthetic>", &reader, &report);
    if (status != ACGC_MACOS_HOST_UNSUPPORTED_DISC_ID ||
        strcmp(report.disc_id, "GAFE02") != 0) {
        fprintf(stderr, "host self-test: unsupported disc ID was accepted\n");
        return 1;
    }
    memcpy(image, "GAFE01", 6);
    status = validate_reader("<synthetic lowercase check>", "<synthetic>", &reader, &report);
    if (status != ACGC_MACOS_HOST_OK) {
        fprintf(stderr, "host self-test: exact supported disc ID was rejected\n");
        return 1;
    }
    printf("host self-test: PASS (options, exact GAFE01 ID, GCM, DOL, FST)\n");
    return 0;
}
