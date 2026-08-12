#include "acgc/macos_host.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int test_option_paths(void) {
    AcgcMacosHostOptions options;
    char error[ACGC_MACOS_HOST_ERROR_CAPACITY];
    const char* argv[] = { "host", "--disc", "/tmp/explicit.gcm", "--headless" };
    const char* bad_argv[] = { "host", "--disc" };
    const char* missing_deadline_argv[] = { "host", "--verify-frames", "1" };

    CHECK(acgc_macos_host_parse_options(
        (int)(sizeof(argv) / sizeof(argv[0])), argv, &options, error, sizeof(error)));
    CHECK(options.disc_path != NULL);
    CHECK(strcmp(options.disc_path, "/tmp/explicit.gcm") == 0);
    CHECK(options.headless);
    CHECK(!acgc_macos_host_parse_options(
        (int)(sizeof(bad_argv) / sizeof(bad_argv[0])),
        bad_argv,
        &options,
        error,
        sizeof(error)));
    CHECK(!acgc_macos_host_parse_options(
        (int)(sizeof(missing_deadline_argv) / sizeof(missing_deadline_argv[0])),
        missing_deadline_argv,
        &options,
        error,
        sizeof(error)));
    CHECK(strstr(error, "deadline") != NULL);
    return 0;
}

static int test_path_limit(void) {
    char path[ACGC_MACOS_HOST_PATH_CAPACITY + 1];
    AcgcMacosDiscReport report;

    memset(path, 'x', sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    CHECK(acgc_macos_host_validate_disc(path, &report) == ACGC_MACOS_HOST_PATH_TOO_LONG);
    return 0;
}

static int test_invalid_argument_ownership_guards(void) {
    AcgcMacosPreparedDisc prepared = { 0 };

    CHECK(acgc_macos_host_validate_disc("/tmp/does-not-matter.gcm", NULL) ==
          ACGC_MACOS_HOST_INVALID_ARGUMENT);

    prepared.images.dol_data = (uint8_t*)(uintptr_t)1;
    prepared.images.rel_data = (uint8_t*)(uintptr_t)2;
    prepared.images.manifest.dol_size = 1;
    prepared.images.rel_size = 1;
    prepared.images.rel_format = ACGC_REL_RAW;
    CHECK(acgc_macos_host_prepare_disc("/tmp/does-not-matter.gcm", &prepared) ==
          ACGC_MACOS_HOST_INVALID_ARGUMENT);
    CHECK(prepared.images.dol_data == (uint8_t*)(uintptr_t)1);
    CHECK(prepared.images.rel_data == (uint8_t*)(uintptr_t)2);
    CHECK(prepared.images.manifest.dol_size == 1);
    CHECK(prepared.images.rel_size == 1);
    return 0;
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

static int write_all(int fd, const uint8_t* bytes, size_t size) {
    size_t written = 0;

    while (written < size) {
        ssize_t result = write(fd, bytes + written, size - written);
        if (result <= 0) {
            return 0;
        }
        written += (size_t)result;
    }
    return 1;
}

static int test_prepared_disc_ownership(void) {
    static const char fst_strings[] = "dir\0nested.bin\0foresta.rel.szs\0";
    uint8_t image[0x2000];
    char path[] = "/tmp/acgc-prepared-disc-XXXXXX";
    AcgcMacosPreparedDisc prepared = { 0 };
    AcgcMacosDiscReport report;
    int fd;

    memset(image, 0, sizeof(image));
    memcpy(image, "GAFE01\0\0", ACGC_BOOT_SOURCE_REVISION_SIZE);
    store_be32(image + 0x1C, UINT32_C(0xC2339F3D));
    store_be32(image + 0x420, 0x500);
    store_be32(image + 0x424, 0x700);
    store_be32(image + 0x428, 0x80);
    store_be32(image + 0x42C, 0x80);
    store_be32(image + 0x500, 0xE4);
    store_be32(image + 0x590, 4);
    memcpy(image + 0x5E4, "DOL!", 4);
    store_fst_entry(image + 0x700, 1, 0, 0, 4);
    store_fst_entry(image + 0x70C, 1, 0, 0, 3);
    store_fst_entry(image + 0x718, 0, 4, 0xA00, 3);
    store_fst_entry(image + 0x724, 0, 15, 0x800, 8);
    memcpy(image + 0x730, fst_strings, sizeof(fst_strings));
    memcpy(image + 0xA00, "DAT", 3);
    memcpy(image + 0x800, "REL\0\x10\x20\x30\x40", 8);

    fd = mkstemp(path);
    CHECK(fd >= 0);
    CHECK(write_all(fd, image, sizeof(image)));
    CHECK(close(fd) == 0);

    CHECK(acgc_macos_host_prepare_disc(path, &prepared) == ACGC_MACOS_HOST_OK);
    CHECK(prepared.report.status == ACGC_MACOS_HOST_OK);
    CHECK(prepared.images.dol_data != NULL);
    CHECK(prepared.images.rel_data != NULL);
    CHECK(prepared.images.manifest.dol_size == 0xE8);
    CHECK(prepared.images.rel_size == 8);
    CHECK(memcmp(prepared.images.dol_data + 0xE4, "DOL!", 4) == 0);
    CHECK(memcmp(prepared.images.rel_data, "REL\0\x10\x20\x30\x40", 8) == 0);

    CHECK(acgc_macos_host_prepare_disc(path, &prepared) ==
          ACGC_MACOS_HOST_INVALID_ARGUMENT);
    CHECK(prepared.images.dol_data != NULL);
    CHECK(prepared.images.rel_data != NULL);
    acgc_macos_host_dispose_prepared_disc(&prepared);
    acgc_macos_host_dispose_prepared_disc(&prepared);
    CHECK(prepared.images.dol_data == NULL);
    CHECK(prepared.images.rel_data == NULL);

    CHECK(acgc_macos_host_validate_disc(path, &report) == ACGC_MACOS_HOST_OK);
    CHECK(report.status == ACGC_MACOS_HOST_OK);
    CHECK(report.dol_size == 0xE8);
    CHECK(report.rel_output_size == 8);
    CHECK(unlink(path) == 0);
    return 0;
}

int main(void) {
    CHECK(test_option_paths() == 0);
    CHECK(test_path_limit() == 0);
    CHECK(test_invalid_argument_ownership_guards() == 0);
    CHECK(test_prepared_disc_ownership() == 0);
    CHECK(acgc_macos_host_run_self_test() == 0);
    puts("host core tests: PASS");
    return 0;
}
