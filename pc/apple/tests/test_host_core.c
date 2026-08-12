#include "acgc/macos_host.h"

#include <stdio.h>
#include <string.h>

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

int main(void) {
    CHECK(test_option_paths() == 0);
    CHECK(test_path_limit() == 0);
    CHECK(acgc_macos_host_run_self_test() == 0);
    puts("host core tests: PASS");
    return 0;
}
