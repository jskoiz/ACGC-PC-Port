#include <dolphin/os/OSThread.h>
#include <dolphin/card.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        goto cleanup; \
    } \
} while (0)

static int all_bytes_equal(const unsigned char* bytes, size_t count, unsigned char value) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (bytes[i] != value) return 0;
    }
    return 1;
}

int main(void) {
    char original_cwd[PATH_MAX];
    char temp_dir[] = "/private/tmp/acgc-card-roundtrip-XXXXXX";
    CARDFileInfo file_info = { 0 };
    CARDFileInfo reopened_info = { 0 };
    unsigned char bytes[16];
    const unsigned char first_write[] = { 'S', 'A', 'V', 'E' };
    const unsigned char second_write[] = { 'C', 'A', 'R', 'D' };
    const unsigned char expected[] = { 'C', 'A', 'R', 'D', 'S', 'A', 'V', 'E' };
    char readback[sizeof(expected)];
    int result = 1;

    if (!getcwd(original_cwd, sizeof(original_cwd))) {
        perror("getcwd");
        return 1;
    }
    if (!mkdtemp(temp_dir)) {
        perror("mkdtemp");
        return 1;
    }
    if (chdir(temp_dir) != 0) {
        perror("chdir");
        rmdir(temp_dir);
        return 1;
    }

    CARDInit();
    CHECK(access("save/card_a", F_OK) == 0);
    CHECK(access("save/card_b", F_OK) == 0);

    /* Host files are unavailable until the corresponding CARD channel is mounted. */
    CHECK(CARDCreate(0, "roundtrip.gci", sizeof(bytes), &file_info) == CARD_RESULT_NOCARD);
    CHECK(access("save/card_a/roundtrip.gci", F_OK) != 0);
    CHECK(CARDMount(2, NULL, NULL) == CARD_RESULT_NOCARD);
    CHECK(CARDMount(0, NULL, NULL) == CARD_RESULT_READY);
    CHECK(CARDCreate(0, "oversize.gci", 0x80000000u, &file_info) == CARD_RESULT_INSSPACE);
    CHECK(access("save/card_a/oversize.gci", F_OK) != 0);

    CHECK(CARDCreate(0, "roundtrip.gci", sizeof(bytes), &file_info) == CARD_RESULT_READY);
    memset(bytes, 0xA5, sizeof(bytes));
    CHECK(CARDRead(&file_info, bytes, sizeof(bytes), 0) == CARD_RESULT_READY);
    CHECK(all_bytes_equal(bytes, sizeof(bytes), 0));
    CHECK(file_info.offset == (s32)sizeof(bytes));

    /* Non-zero offsets must address the fixed CARD file, not the previous stdio cursor. */
    CHECK(CARDWrite(&file_info, first_write, sizeof(first_write), 4) == CARD_RESULT_READY);
    CHECK(file_info.offset == 8);
    CHECK(CARDWrite(&file_info, second_write, sizeof(second_write), 0) == CARD_RESULT_READY);
    CHECK(CARDRead(&file_info, readback, sizeof(readback), 0) == CARD_RESULT_READY);
    CHECK(memcmp(readback, expected, sizeof(expected)) == 0);

    /* Negative and overflowing ranges are rejected before touching the host file. */
    CHECK(CARDRead(&file_info, readback, 1, -1) == CARD_RESULT_IOERROR);
    CHECK(CARDRead(&file_info, readback, 2, (s32)sizeof(bytes) - 1) == CARD_RESULT_IOERROR);
    CHECK(CARDWrite(&file_info, second_write, 2, (s32)sizeof(bytes) - 1) == CARD_RESULT_IOERROR);
    CHECK(CARDWrite(&file_info, NULL, 1, 0) == CARD_RESULT_IOERROR);

    CHECK(CARDClose(&file_info) == CARD_RESULT_READY);
    CHECK(CARDOpen(0, "roundtrip.gci", &reopened_info) == CARD_RESULT_READY);
    memset(readback, 0, sizeof(readback));
    CHECK(CARDRead(&reopened_info, readback, sizeof(readback), 0) == CARD_RESULT_READY);
    CHECK(memcmp(readback, expected, sizeof(expected)) == 0);
    CHECK(CARDClose(&reopened_info) == CARD_RESULT_READY);

    CHECK(CARDUnmount(0) == CARD_RESULT_READY);
    CHECK(CARDOpen(0, "roundtrip.gci", &reopened_info) == CARD_RESULT_NOCARD);
    CHECK(CARDCreate(0, "../escape.gci", sizeof(bytes), &file_info) == CARD_RESULT_NOCARD);
    CHECK(CARDMount(0, NULL, NULL) == CARD_RESULT_READY);
    CHECK(CARDCreate(0, "../escape.gci", sizeof(bytes), &file_info) == CARD_RESULT_NAMETOOLONG);

    result = 0;

cleanup:
    CARDUnmount(0);
    CARDClose(&file_info);
    CARDClose(&reopened_info);
    unlink("save/card_a/roundtrip.gci");
    rmdir("save/card_a");
    rmdir("save/card_b");
    rmdir("save");
    if (chdir(original_cwd) != 0) {
        perror("restore cwd");
        return 1;
    }
    if (rmdir(temp_dir) != 0) {
        perror("rmdir temp dir");
        return 1;
    }
    if (result == 0) puts("pc CARD save/load round-trip: PASS");
    return result;
}
