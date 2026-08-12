#include "acgc/macos_host.h"

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

static const char ACGC_MACOS_HOST_VERIFY_SECONDS_PREFIX[] = "--verify-seconds=";
static const char ACGC_MACOS_HOST_VERIFY_FRAMES_PREFIX[] = "--verify-frames=";

typedef struct AcgcMacosFileReader {
    int fd;
    uint32_t size;
} AcgcMacosFileReader;

typedef struct AcgcMacosMemoryReader {
    const uint8_t* bytes;
    uint32_t size;
} AcgcMacosMemoryReader;

static int boot_source_images_are_zero(const AcgcBootSourceImages* images) {
    uint32_t i;

    if (images == NULL || images->dol_data != NULL || images->rel_data != NULL ||
        images->manifest.dol_offset != 0 || images->manifest.dol_size != 0 ||
        images->manifest.fst_file_count != 0 ||
        images->manifest.rel_input_offset != 0 ||
        images->manifest.rel_input_size != 0 || images->rel_size != 0 ||
        images->rel_format != ACGC_REL_RAW) {
        return 0;
    }
    for (i = 0; i < ACGC_BOOT_SOURCE_REVISION_SIZE; i++) {
        if (images->manifest.revision[i] != 0) {
            return 0;
        }
    }
    return 1;
}

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
    report->boot_source_status = ACGC_BOOT_SOURCE_INVALID_ARGUMENT;
}

static AcgcMacosHostStatus fail_report(
    AcgcMacosDiscReport* report,
    AcgcMacosHostStatus status,
    AcgcBootSourceStatus boot_source_status,
    const char* format,
    ...
) {
    va_list args;

    if (report != NULL) {
        report->status = status;
        report->boot_source_status = boot_source_status;
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

static AcgcMacosHostStatus validate_reader(
    const char* display_path,
    const char* resolved_path,
    const AcgcDiscReader* reader,
    AcgcMacosDiscReport* report,
    AcgcBootSourceImages* retained_images
) {
    AcgcBootSourceImages images = { 0 };
    AcgcBootSourceStatus boot_source_status;

    reset_report(report);
    if (report == NULL || reader == NULL || reader->read == NULL ||
        display_path == NULL ||
        (retained_images != NULL && !boot_source_images_are_zero(retained_images))) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_INVALID_ARGUMENT,
            ACGC_BOOT_SOURCE_INVALID_ARGUMENT,
            retained_images != NULL && !boot_source_images_are_zero(retained_images)
                ? "retained boot-source output must be zero-initialized"
                : "a disc path and bounded reader are required"
        );
    }
    if (!copy_string(report->input_path, sizeof(report->input_path), display_path)) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_PATH_TOO_LONG,
            ACGC_BOOT_SOURCE_INVALID_ARGUMENT,
            "disc path exceeds %u bytes",
            (unsigned)(sizeof(report->input_path) - 1)
        );
    }
    if (resolved_path != NULL &&
        !copy_string(report->resolved_path, sizeof(report->resolved_path), resolved_path)) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_PATH_TOO_LONG,
            ACGC_BOOT_SOURCE_INVALID_ARGUMENT,
            "resolved disc path exceeds %u bytes",
            (unsigned)(sizeof(report->resolved_path) - 1)
        );
    }
    report->file_size = reader->size;

    boot_source_status = acgc_boot_source_prepare(reader, NULL, &images);
    report->boot_source_status = boot_source_status;
    if (boot_source_status != ACGC_BOOT_SOURCE_OK) {
        AcgcMacosHostStatus host_status = fail_report(
            report,
            ACGC_MACOS_HOST_BOOT_SOURCE_FAILED,
            boot_source_status,
            "bounded boot-source preparation failed: %s",
            acgc_boot_source_status_string(boot_source_status)
        );
        acgc_boot_source_dispose(&images);
        return host_status;
    }

    memcpy(
        report->revision,
        images.manifest.revision,
        sizeof(report->revision)
    );
    report->dol_size = images.manifest.dol_size;
    report->fst_file_count = images.manifest.fst_file_count;
    report->rel_input_size = images.manifest.rel_input_size;
    report->rel_output_size = images.rel_size;
    report->rel_format = images.rel_format;
    report->status = ACGC_MACOS_HOST_OK;
    report->boot_source_status = ACGC_BOOT_SOURCE_OK;
    if (retained_images != NULL) {
        *retained_images = images;
        memset(&images, 0, sizeof(images));
    }
    acgc_boot_source_dispose(&images);
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
        } else if (strcmp(argument, "--verify-frames") == 0 ||
                   strncmp(
                       argument,
                       ACGC_MACOS_HOST_VERIFY_FRAMES_PREFIX,
                       sizeof(ACGC_MACOS_HOST_VERIFY_FRAMES_PREFIX) - 1
                   ) == 0) {
            const char* value = strncmp(
                    argument,
                    ACGC_MACOS_HOST_VERIFY_FRAMES_PREFIX,
                    sizeof(ACGC_MACOS_HOST_VERIFY_FRAMES_PREFIX) - 1
                ) == 0
                ? argument + sizeof(ACGC_MACOS_HOST_VERIFY_FRAMES_PREFIX) - 1
                : NULL;
            char* end = NULL;
            unsigned long frames;

            if (value == NULL) {
                if (i + 1 >= argc || argv[i + 1] == NULL) {
                    set_error(error, error_capacity, "--verify-frames requires a positive integer");
                    return 0;
                }
                value = argv[++i];
            }
            errno = 0;
            frames = strtoul(value, &end, 10);
            if (errno != 0 || end == value || *end != '\0' || frames == 0 ||
                frames > ACGC_MACOS_HOST_MAX_VERIFY_FRAMES) {
                set_error(
                    error,
                    error_capacity,
                    "--verify-frames must be in [1, %u]",
                    ACGC_MACOS_HOST_MAX_VERIFY_FRAMES
                );
                return 0;
            }
            options->verify_frames = (uint32_t)frames;
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
    if (options->headless && options->verify_frames > 0) {
        set_error(error, error_capacity, "--verify-frames requires a foreground Metal host");
        return 0;
    }
    if (options->verify_frames > 0 && options->verify_seconds <= 0.0) {
        set_error(
            error,
            error_capacity,
            "--verify-frames requires a positive --verify-seconds deadline"
        );
        return 0;
    }
    return 1;
}

const char* acgc_macos_host_usage(void) {
    return
        "Usage: acgc_macos_native_host [--disc PATH] [--verify-frames N --verify-seconds S]\n"
        "       acgc_macos_native_host --headless --disc PATH\n"
        "       acgc_macos_native_host --self-test\n"
        "\n"
        "The host accepts one explicit read-only ISO/GCM path and never searches\n"
        "for or embeds proprietary game data. --verify-seconds is bounded to 60s.\n"
        "--verify-frames requests command-buffer-completed native Metal geometry fixture frames and\n"
        "is bounded to 600 frames; pair it with --verify-seconds for a deadline.\n";
}

AcgcMacosHostStatus acgc_macos_host_prepare_disc(
    const char* path,
    AcgcMacosPreparedDisc* prepared
) {
    AcgcMacosFileReader file_reader;
    AcgcDiscReader reader;
    struct stat file_stat;
    char resolved_path[ACGC_MACOS_HOST_PATH_CAPACITY];
    AcgcMacosDiscReport* report;
    int flags = O_RDONLY;
    int fd;

    if (prepared == NULL) {
        return ACGC_MACOS_HOST_INVALID_ARGUMENT;
    }
    if (!boot_source_images_are_zero(&prepared->images)) {
        return fail_report(
            &prepared->report,
            ACGC_MACOS_HOST_INVALID_ARGUMENT,
            ACGC_BOOT_SOURCE_INVALID_ARGUMENT,
            "prepared disc must be disposed before reuse"
        );
    }
    report = &prepared->report;
    reset_report(report);
    if (path == NULL || path[0] == '\0') {
        return fail_report(
            report,
            ACGC_MACOS_HOST_INVALID_ARGUMENT,
            ACGC_BOOT_SOURCE_INVALID_ARGUMENT,
            "an explicit non-empty disc path is required"
        );
    }
    if (strlen(path) >= ACGC_MACOS_HOST_PATH_CAPACITY) {
        return fail_report(
            report,
            ACGC_MACOS_HOST_PATH_TOO_LONG,
            ACGC_BOOT_SOURCE_INVALID_ARGUMENT,
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
            ACGC_BOOT_SOURCE_READ_FAILED,
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
            ACGC_BOOT_SOURCE_READ_FAILED,
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
            ACGC_BOOT_SOURCE_INVALID_ARGUMENT,
            "disc path '%s' is not a regular file",
            path
        );
    }
    if (file_stat.st_size < 0 || (uint64_t)file_stat.st_size > UINT32_MAX) {
        close(fd);
        return fail_report(
            report,
            ACGC_MACOS_HOST_UNSUPPORTED_FILE,
            ACGC_BOOT_SOURCE_INVALID_RANGE,
            "disc file size is outside the bounded 32-bit portable-reader range"
        );
    }
    if (realpath(path, resolved_path) == NULL) {
        int saved_errno = errno;
        close(fd);
        return fail_report(
            report,
            ACGC_MACOS_HOST_OPEN_FAILED,
            ACGC_BOOT_SOURCE_READ_FAILED,
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
            report,
            &prepared->images
        );
        close(fd);
        return status;
    }
}

AcgcMacosHostStatus acgc_macos_host_validate_disc(
    const char* path,
    AcgcMacosDiscReport* report
) {
    AcgcMacosPreparedDisc prepared = { 0 };
    AcgcMacosHostStatus status;

    if (report == NULL) {
        return ACGC_MACOS_HOST_INVALID_ARGUMENT;
    }
    status = acgc_macos_host_prepare_disc(path, &prepared);
    *report = prepared.report;
    acgc_macos_host_dispose_prepared_disc(&prepared);
    return status;
}

void acgc_macos_host_dispose_prepared_disc(
    AcgcMacosPreparedDisc* prepared
) {
    if (prepared == NULL) {
        return;
    }
    acgc_boot_source_dispose(&prepared->images);
    memset(prepared, 0, sizeof(*prepared));
}

const char* acgc_macos_host_status_string(AcgcMacosHostStatus status) {
    switch (status) {
        case ACGC_MACOS_HOST_OK: return "ok";
        case ACGC_MACOS_HOST_INVALID_ARGUMENT: return "invalid argument";
        case ACGC_MACOS_HOST_PATH_TOO_LONG: return "path too long";
        case ACGC_MACOS_HOST_OPEN_FAILED: return "open failed";
        case ACGC_MACOS_HOST_STAT_FAILED: return "stat failed";
        case ACGC_MACOS_HOST_UNSUPPORTED_FILE: return "unsupported file";
        case ACGC_MACOS_HOST_BOOT_SOURCE_FAILED: return "boot-source preparation failed";
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

static const char* rel_format_string(AcgcRelFormat format) {
    switch (format) {
        case ACGC_REL_RAW: return "raw";
        case ACGC_REL_YAZ0: return "Yaz0";
    }
    return "unknown";
}

static void append_revision_bytes(
    char* output,
    size_t capacity,
    size_t* length,
    const uint8_t* revision
) {
    if (revision == NULL) {
        append_status(output, capacity, length, "Revision bytes: unavailable\n");
        return;
    }
    append_status(
        output,
        capacity,
        length,
        "Revision bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n",
        (unsigned)revision[0],
        (unsigned)revision[1],
        (unsigned)revision[2],
        (unsigned)revision[3],
        (unsigned)revision[4],
        (unsigned)revision[5],
        (unsigned)revision[6],
        (unsigned)revision[7]
    );
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
            "Host status: %s\n"
            "Boot-source status: %s\n",
            acgc_macos_host_status_string(report->status),
            acgc_boot_source_status_string(report->boot_source_status));
        if (report->status == ACGC_MACOS_HOST_OK) {
            append_status(output, output_capacity, &length,
                "Bounded boot-source preparation: succeeded\n");
            append_revision_bytes(
                output,
                output_capacity,
                &length,
                report->revision
            );
            append_status(output, output_capacity, &length,
                "DOL: prepared (%u bytes)\n"
                "FST: inspected (%u files)\n"
                "REL: prepared (%u input -> %u output bytes, %s)\n",
                report->dol_size,
                report->fst_file_count,
                report->rel_input_size,
                report->rel_output_size,
                rel_format_string(report->rel_format));
        } else if (report->error[0] != '\0') {
            append_status(output, output_capacity, &length,
                "Bounded boot-source preparation: failed\n");
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
        "\nCapability gates:\n"
        "  Native Metal command-buffer-completed geometry fixture (clear/triangle/present): foreground path; verify with --verify-frames N --verify-seconds S\n"
        "  Game execution: not implemented\n"
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

#define ACGC_MACOS_HOST_SELF_TEST_IMAGE_SIZE UINT32_C(0x2000)
#define ACGC_MACOS_HOST_SELF_TEST_DOL_OFFSET UINT32_C(0x500)
#define ACGC_MACOS_HOST_SELF_TEST_FST_OFFSET UINT32_C(0x700)
#define ACGC_MACOS_HOST_SELF_TEST_FST_SIZE UINT32_C(0x80)
#define ACGC_MACOS_HOST_SELF_TEST_RAW_REL_OFFSET UINT32_C(0x800)
#define ACGC_MACOS_HOST_SELF_TEST_YAZ0_REL_OFFSET UINT32_C(0x900)

static const uint8_t acgc_macos_host_self_test_yaz0_rel[] = {
    'Y', 'a', 'z', '0',
    0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0xE0, 'A', 'B', 'C'
};

static const char acgc_macos_host_self_test_fst_strings[] =
    "dir\0nested.bin\0foresta.rel.szs\0";

static void set_self_test_rel(uint8_t* image, int yaz0) {
    uint8_t* fst = image + ACGC_MACOS_HOST_SELF_TEST_FST_OFFSET;

    store_fst_entry(
        fst + 36,
        0,
        15,
        yaz0 ? ACGC_MACOS_HOST_SELF_TEST_YAZ0_REL_OFFSET :
               ACGC_MACOS_HOST_SELF_TEST_RAW_REL_OFFSET,
        yaz0 ? (uint32_t)sizeof(acgc_macos_host_self_test_yaz0_rel) : 8
    );
}

static void make_self_test_image(uint8_t* image, size_t image_size) {
    uint8_t* fst;
    uint8_t* dol;

    memset(image, 0, image_size);
    memcpy(image, "GAFE01\0\0", ACGC_BOOT_SOURCE_REVISION_SIZE);
    store_be32(image + 0x1C, UINT32_C(0xC2339F3D));
    store_be32(image + 0x420, ACGC_MACOS_HOST_SELF_TEST_DOL_OFFSET);
    store_be32(image + 0x424, ACGC_MACOS_HOST_SELF_TEST_FST_OFFSET);
    store_be32(image + 0x428, ACGC_MACOS_HOST_SELF_TEST_FST_SIZE);
    store_be32(image + 0x42C, ACGC_MACOS_HOST_SELF_TEST_FST_SIZE);

    dol = image + ACGC_MACOS_HOST_SELF_TEST_DOL_OFFSET;
    store_be32(dol + 0x00, UINT32_C(0xE4));
    store_be32(dol + 0x90, UINT32_C(4));
    memcpy(dol + 0xE4, "DOL!", 4);

    fst = image + ACGC_MACOS_HOST_SELF_TEST_FST_OFFSET;
    store_fst_entry(fst + 0x00, 1, 0, 0, 4);
    store_fst_entry(fst + 0x0C, 1, 0, 0, 3);
    store_fst_entry(fst + 0x18, 0, 4, 0xA00, 3);
    set_self_test_rel(image, 0);
    memcpy(
        fst + 0x30,
        acgc_macos_host_self_test_fst_strings,
        sizeof(acgc_macos_host_self_test_fst_strings)
    );
    memcpy(image + 0xA00, "DAT", 3);
    memcpy(image + ACGC_MACOS_HOST_SELF_TEST_RAW_REL_OFFSET,
           "REL\0\x10\x20\x30\x40", 8);
    memcpy(image + ACGC_MACOS_HOST_SELF_TEST_YAZ0_REL_OFFSET,
           acgc_macos_host_self_test_yaz0_rel,
           sizeof(acgc_macos_host_self_test_yaz0_rel));
}

static int self_test_prepare_images(void) {
    uint8_t image[ACGC_MACOS_HOST_SELF_TEST_IMAGE_SIZE];
    AcgcMacosMemoryReader memory_reader;
    AcgcDiscReader reader;
    AcgcBootSourceImages images = { 0 };

    make_self_test_image(image, sizeof(image));
    memory_reader.bytes = image;
    memory_reader.size = (uint32_t)sizeof(image);
    reader.context = &memory_reader;
    reader.size = memory_reader.size;
    reader.read = memory_reader_read;
    if (acgc_boot_source_prepare(&reader, NULL, &images) != ACGC_BOOT_SOURCE_OK ||
        images.dol_data == NULL || images.rel_data == NULL ||
        images.rel_size != 8 || images.rel_format != ACGC_REL_RAW ||
        memcmp(images.dol_data + 0xE4, "DOL!", 4) != 0 ||
        memcmp(images.rel_data, "REL\0\x10\x20\x30\x40", 8) != 0) {
        acgc_boot_source_dispose(&images);
        return 0;
    }
    acgc_boot_source_dispose(&images);
    if (!boot_source_images_are_zero(&images)) {
        return 0;
    }

    make_self_test_image(image, sizeof(image));
    set_self_test_rel(image, 1);
    if (acgc_boot_source_prepare(&reader, NULL, &images) != ACGC_BOOT_SOURCE_OK ||
        images.dol_data == NULL || images.rel_data == NULL ||
        images.rel_size != 3 || images.rel_format != ACGC_REL_YAZ0 ||
        memcmp(images.rel_data, "ABC", 3) != 0) {
        acgc_boot_source_dispose(&images);
        return 0;
    }
    acgc_boot_source_dispose(&images);
    return boot_source_images_are_zero(&images);
}

static int self_test_options(void) {
    AcgcMacosHostOptions options;
    char error[ACGC_MACOS_HOST_ERROR_CAPACITY];
    const char* valid_argv[] = {
        "host", "--disc", "/tmp/GAFE01.gcm", "--verify-frames", "2",
        "--verify-seconds", "0.25"
    };
    const char* equals_argv[] = {
        "host", "--disc=/tmp/GAFE01.gcm", "--verify-frames=3", "--verify-seconds=0.5"
    };
    const char* invalid_argv[] = { "host", "--verify-frames", "0" };
    const char* missing_deadline_argv[] = { "host", "--verify-frames", "1" };

    if (!acgc_macos_host_parse_options(
            (int)(sizeof(valid_argv) / sizeof(valid_argv[0])),
            valid_argv,
            &options,
            error,
            sizeof(error)) ||
        options.disc_path == NULL ||
        strcmp(options.disc_path, "/tmp/GAFE01.gcm") != 0 ||
        options.verify_frames != 2 || options.verify_seconds != 0.25 || options.headless) {
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
        options.verify_frames != 3 || options.verify_seconds != 0.5) {
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
    if (acgc_macos_host_parse_options(
            (int)(sizeof(missing_deadline_argv) / sizeof(missing_deadline_argv[0])),
            missing_deadline_argv,
            &options,
            error,
            sizeof(error)) ||
        strstr(error, "deadline") == NULL) {
        return 0;
    }
    return 1;
}

int acgc_macos_host_run_self_test(void) {
    uint8_t image[ACGC_MACOS_HOST_SELF_TEST_IMAGE_SIZE];
    AcgcMacosMemoryReader memory_reader;
    AcgcDiscReader reader;
    AcgcMacosDiscReport report;
    AcgcMacosHostStatus status;
    char status_text[4096];
    static const uint8_t expected_revision[ACGC_BOOT_SOURCE_REVISION_SIZE] = {
        'G', 'A', 'F', 'E', '0', '1', 0, 0
    };

    if (!self_test_options()) {
        fprintf(stderr, "host self-test: option parsing failed\n");
        return 1;
    }
    if (!self_test_prepare_images()) {
        fprintf(stderr, "host self-test: bounded raw/Yaz0 preparation failed\n");
        return 1;
    }
    make_self_test_image(image, sizeof(image));
    memory_reader.bytes = image;
    memory_reader.size = (uint32_t)sizeof(image);
    reader.context = &memory_reader;
    reader.size = memory_reader.size;
    reader.read = memory_reader_read;

    status = validate_reader(
        "<synthetic GAFE01_00 raw>",
        "<synthetic>",
        &reader,
        &report,
        NULL
    );
    if (status != ACGC_MACOS_HOST_OK ||
        report.boot_source_status != ACGC_BOOT_SOURCE_OK ||
        memcmp(report.revision, expected_revision, sizeof(expected_revision)) != 0 ||
        report.dol_size != 0xE8 || report.fst_file_count != 2 ||
        report.rel_input_size != 8 || report.rel_output_size != 8 ||
        report.rel_format != ACGC_REL_RAW) {
        fprintf(stderr, "host self-test: exact GAFE01_00 raw preparation failed\n");
        return 1;
    }
    acgc_macos_host_format_status(NULL, &report, status_text, sizeof(status_text));
    if (strstr(status_text, "Bounded boot-source preparation: succeeded") == NULL ||
        strstr(status_text, "Revision bytes: 47 41 46 45 30 31 00 00") == NULL ||
        strstr(status_text, "Game execution: not implemented") == NULL ||
        strstr(status_text, "REL: prepared (8 input -> 8 output bytes, raw)") == NULL) {
        fprintf(stderr, "host self-test: successful status wording failed\n");
        return 1;
    }

    set_self_test_rel(image, 1);
    status = validate_reader(
        "<synthetic GAFE01_00 Yaz0>",
        "<synthetic>",
        &reader,
        &report,
        NULL
    );
    if (status != ACGC_MACOS_HOST_OK ||
        report.boot_source_status != ACGC_BOOT_SOURCE_OK ||
        report.rel_input_size != sizeof(acgc_macos_host_self_test_yaz0_rel) ||
        report.rel_output_size != 3 || report.rel_format != ACGC_REL_YAZ0) {
        fprintf(stderr, "host self-test: bounded Yaz0 preparation failed\n");
        return 1;
    }

    make_self_test_image(image, sizeof(image));
    image[6] = 1;
    status = validate_reader(
        "<synthetic nonzero disc number>",
        "<synthetic>",
        &reader,
        &report,
        NULL
    );
    if (status != ACGC_MACOS_HOST_BOOT_SOURCE_FAILED ||
        report.boot_source_status != ACGC_BOOT_SOURCE_UNSUPPORTED_REVISION) {
        fprintf(stderr, "host self-test: nonzero disc number was accepted\n");
        return 1;
    }

    make_self_test_image(image, sizeof(image));
    image[7] = 1;
    status = validate_reader(
        "<synthetic nonzero version>",
        "<synthetic>",
        &reader,
        &report,
        NULL
    );
    if (status != ACGC_MACOS_HOST_BOOT_SOURCE_FAILED ||
        report.boot_source_status != ACGC_BOOT_SOURCE_UNSUPPORTED_REVISION) {
        fprintf(stderr, "host self-test: nonzero version was accepted\n");
        return 1;
    }

    make_self_test_image(image, sizeof(image));
    store_fst_entry(
        image + ACGC_MACOS_HOST_SELF_TEST_FST_OFFSET + 36,
        0,
        4,
        ACGC_MACOS_HOST_SELF_TEST_RAW_REL_OFFSET,
        8
    );
    status = validate_reader(
        "<synthetic missing REL>",
        "<synthetic>",
        &reader,
        &report,
        NULL
    );
    if (status != ACGC_MACOS_HOST_BOOT_SOURCE_FAILED ||
        report.boot_source_status != ACGC_BOOT_SOURCE_MISSING_REL) {
        fprintf(stderr, "host self-test: missing foresta.rel.szs was accepted\n");
        return 1;
    }

    printf("host self-test: PASS (options, exact GAFE01_00, bounded raw/Yaz0 REL, rejection paths)\n");
    return 0;
}
