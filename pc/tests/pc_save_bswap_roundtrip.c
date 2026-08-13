#define _POSIX_C_SOURCE 200809L

#include "pc_save_bswap.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* The decomp's host headers intentionally shadow the system stdlib header. */
extern char* mkdtemp(char* template_name);

enum {
    SAVE_FIRST_DELIVERY_OFFSET = 0xB4,
    SAVE_TIME_LIMIT_OFFSET = 0xB6,
    SAVE_CHECKSUM_OFFSET = 0x12,
};

_Static_assert(sizeof(Save_t) == 0x242A0, "Save_t wire size changed");
_Static_assert(sizeof(Save) == 0x26000, "aligned Save size changed");
_Static_assert(offsetof(Save_t, save_check) + offsetof(mFRm_chk_t, checksum) ==
                   SAVE_CHECKSUM_OFFSET,
               "Save_t checksum offset changed");
_Static_assert(offsetof(Save_t, private_data) == 0x20, "Private_c offset changed");
_Static_assert(offsetof(Private_c, deliveries) == 0x94, "delivery offset changed");
_Static_assert(offsetof(mQst_delivery_c, base) == 0x00, "delivery base offset changed");
_Static_assert(offsetof(mQst_base_c, time_limit) == 0x02, "quest time offset changed");
_Static_assert(offsetof(Save_t, private_data) + offsetof(Private_c, deliveries) +
                   offsetof(mQst_delivery_c, base) == SAVE_FIRST_DELIVERY_OFFSET,
               "first delivery wire offset changed");
_Static_assert(SAVE_TIME_LIMIT_OFFSET - SAVE_FIRST_DELIVERY_OFFSET == 0x02,
               "first delivery time offset changed");

/* pc_save_bswap.c retains the decomp's logging calls in the focused object. */
void OSReport(const char* format, ...) {
    (void)format;
}

static int check_condition(int condition, const char* expression, int line) {
    if (!condition) {
        fprintf(stderr, "CHECK failed at line %d: %s\n", line, expression);
        return 0;
    }
    return 1;
}

#define CHECK(condition) \
    do { \
        if (!check_condition((condition), #condition, __LINE__)) return 1; \
    } while (0)

static void put_be16(u8* bytes, u16 value) {
    bytes[0] = (u8)(value >> 8);
    bytes[1] = (u8)value;
}

static void put_be32(u8* bytes, u32 value) {
    bytes[0] = (u8)(value >> 24);
    bytes[1] = (u8)(value >> 16);
    bytes[2] = (u8)(value >> 8);
    bytes[3] = (u8)value;
}

/* Test-only model of the pre-d1575f0 bug.  The old repack treated the
 * 16-bit bitfield storage as a 32-bit unit and wrote the normalized value
 * back through all four bytes, erasing the time_limit bytes at +0x02/+0x03.
 * This is forensic evidence only; it is not an alternate wire format. */
static void legacy_mQst_base_repack_forensic(u8* bytes) {
    u32 raw = ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16) |
              ((u32)bytes[2] << 8) | bytes[3];
    u32 quest_type = (raw >> 30) & 0x3;
    u32 quest_kind = (raw >> 24) & 0x3F;
    u32 time_limit_enabled = (raw >> 23) & 0x1;
    u32 progress = (raw >> 19) & 0xF;
    u32 give_reward = (raw >> 18) & 0x1;
    u32 unused_bits = (raw >> 16) & 0x3;

    raw = quest_type | (quest_kind << 2) | (time_limit_enabled << 8) |
          (progress << 9) | (give_reward << 13) | (unused_bits << 14);
    bytes[0] = (u8)raw;
    bytes[1] = (u8)(raw >> 8);
    bytes[2] = (u8)(raw >> 16);
    bytes[3] = (u8)(raw >> 24);
}

static u32 sum_be16(const u8* bytes, size_t size) {
    u32 sum = 0;
    size_t i;

    for (i = 0; i < size; i += 2) {
        sum += ((u16)bytes[i] << 8) | bytes[i + 1];
    }
    return sum;
}

static int write_exact(const char* path, const void* data, size_t size) {
    FILE* file = fopen(path, "wb");
    size_t written;
    int success;

    if (file == NULL) {
        perror(path);
        return 0;
    }
    written = fwrite(data, 1, size, file);
    success = written == size;
    if (success && fflush(file) != 0) success = 0;
    if (success && fsync(fileno(file)) != 0) success = 0;
    if (fclose(file) != 0) success = 0;
    if (!success) {
        perror(path);
        return 0;
    }
    return 1;
}

static int read_exact(const char* path, void* data, size_t size) {
    FILE* file = fopen(path, "rb");
    size_t read_size;
    int extra;
    int read_error;
    int close_error;

    if (file == NULL) {
        perror(path);
        return 0;
    }
    read_size = fread(data, 1, size, file);
    extra = fgetc(file);
    read_error = ferror(file);
    close_error = fclose(file);
    if (read_size != size || extra != EOF || read_error || close_error) {
        if (read_error) perror(path);
        else fprintf(stderr, "%s: unexpected file size\n", path);
        return 0;
    }
    return 1;
}

static int run_codec_process(const char* executable, const char* direction,
                             const char* input_path, const char* output_path) {
    pid_t child = fork();
    int status;

    if (child < 0) {
        perror("fork");
        return 0;
    }
    if (child == 0) {
        execl(executable, executable, direction, input_path, output_path,
              (char*)NULL);
        _exit(127);
    }
    if (waitpid(child, &status, 0) < 0) {
        perror("waitpid");
        return 0;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int run_child(int argc, char** argv) {
    Save_t* save;

    if (argc != 4 ||
        (strcmp(argv[1], "from-be") != 0 && strcmp(argv[1], "to-be") != 0)) {
        return 0;
    }
    save = (Save_t*)malloc(sizeof(*save));
    if (save == NULL) {
        perror("malloc");
        return 0;
    }
    if (!read_exact(argv[2], save, sizeof(*save))) {
        free(save);
        return 0;
    }
    pc_save_bswap(save, strcmp(argv[1], "from-be") == 0 ? PC_BSWAP_FROM_BE
                                                          : PC_BSWAP_TO_BE);
    if (!write_exact(argv[3], save, sizeof(*save))) {
        free(save);
        return 0;
    }
    free(save);
    return 1;
}

static int test_known_quest_encoding(void) {
    static const u8 be_bytes[] = {
        0x42, 0x9E, 0xA5, 0x5A, 0x12, 0x34,
        0x56, 0x78, 0x12, 0x34, 0xC0, 0xDE,
    };
    static const u8 le_bytes[] = {
        0x09, 0xA7, 0xA5, 0x5A, 0x12, 0x34,
        0x56, 0x78, 0x34, 0x12, 0xC0, 0xDE,
    };
    Save_t* save = (Save_t*)calloc(1, sizeof(*save));
    u8* bytes;

    CHECK(save != NULL);
    bytes = (u8*)save;
    memcpy(bytes + SAVE_FIRST_DELIVERY_OFFSET, be_bytes, sizeof(be_bytes));
    pc_save_bswap(save, PC_BSWAP_FROM_BE);
    CHECK(memcmp(bytes + SAVE_FIRST_DELIVERY_OFFSET, le_bytes,
                 sizeof(le_bytes)) == 0);
    pc_save_bswap(save, PC_BSWAP_TO_BE);
    CHECK(memcmp(bytes + SAVE_FIRST_DELIVERY_OFFSET, be_bytes,
                 sizeof(be_bytes)) == 0);
    free(save);
    return 0;
}

static int test_pre_fix_raw_wire_loss(void) {
    u8 bytes[] = { 0x42, 0x9E, 0xF1, 0x0E };

    legacy_mQst_base_repack_forensic(bytes);
    CHECK(bytes[0] == 0x09);
    CHECK(bytes[1] == 0xA7);
    CHECK(bytes[2] == 0x00);
    CHECK(bytes[3] == 0x00);
    return 0;
}

static int test_checksum(void) {
    static const u8 vector[] = { 0x00, 0x01, 0x00, 0x02 };
    Save_t* save = (Save_t*)calloc(1, sizeof(*save));
    u8* bytes;
    u16 checksum;

    CHECK(save != NULL);
    CHECK(pc_checksum_be(vector, sizeof(vector), 0) == 0xFFFD);

    bytes = (u8*)save;
    put_be32(bytes + 0x00, 0x13579BDF);
    put_be16(bytes + SAVE_CHECKSUM_OFFSET, 0);
    checksum = pc_checksum_be(bytes, sizeof(*save), 0);
    put_be16(bytes + SAVE_CHECKSUM_OFFSET, checksum);
    CHECK((sum_be16(bytes, sizeof(*save)) & 0xFFFF) == 0);
    CHECK(pc_checksum_be(bytes, sizeof(*save), checksum) == checksum);
    free(save);
    return 0;
}

static void make_fixture(Save_t* save) {
    u8* bytes = (u8*)save;
    size_t i;

    memset(save, 0, sizeof(*save));
    put_be32(bytes + 0x14, 0x11223344);
    put_be16(bytes + 0x1A, 0xA1B2);
    for (i = 0; i < sizeof(mQst_delivery_c); i++) {
        bytes[SAVE_FIRST_DELIVERY_OFFSET + i] = (u8)(0x31 + i * 7);
    }
    /* The pre-d1575f0 repack erased this raw 16-bit wire value. */
    bytes[SAVE_TIME_LIMIT_OFFSET + 0] = 0xF1;
    bytes[SAVE_TIME_LIMIT_OFFSET + 1] = 0x0E;
    put_be16(bytes + SAVE_CHECKSUM_OFFSET, 0);
    put_be16(bytes + SAVE_CHECKSUM_OFFSET,
             pc_checksum_be(bytes, sizeof(*save), 0));
}

static int test_process_restart_roundtrip(const char* executable,
                                           const char* directory) {
    char input_path[PATH_MAX];
    char little_path[PATH_MAX];
    char output_path[PATH_MAX];
    Save_t* original = (Save_t*)malloc(sizeof(*original));
    Save_t* roundtrip = (Save_t*)malloc(sizeof(*roundtrip));
    u8* bytes;

    CHECK(original != NULL);
    CHECK(roundtrip != NULL);
    CHECK(snprintf(input_path, sizeof(input_path), "%s/save-be.bin", directory) <
          (int)sizeof(input_path));
    CHECK(snprintf(little_path, sizeof(little_path), "%s/save-le.bin", directory) <
          (int)sizeof(little_path));
    CHECK(snprintf(output_path, sizeof(output_path), "%s/save-be-roundtrip.bin",
                   directory) < (int)sizeof(output_path));

    make_fixture(original);
    CHECK(write_exact(input_path, original, sizeof(*original)));
    CHECK(run_codec_process(executable, "from-be", input_path, little_path));
    CHECK(run_codec_process(executable, "to-be", little_path, output_path));
    CHECK(read_exact(output_path, roundtrip, sizeof(*roundtrip)));
    CHECK(memcmp(original, roundtrip, sizeof(*original)) == 0);

    bytes = (u8*)roundtrip;
    CHECK(bytes[SAVE_TIME_LIMIT_OFFSET + 0] == 0xF1);
    CHECK(bytes[SAVE_TIME_LIMIT_OFFSET + 1] == 0x0E);
    CHECK(pc_save_bswap_verify_roundtrip(bytes, sizeof(*roundtrip)) == 0);

    unlink(input_path);
    unlink(little_path);
    unlink(output_path);
    free(original);
    free(roundtrip);
    return 0;
}

int main(int argc, char** argv) {
    char process_directory[] = "pc-save-bswap-process-XXXXXX";
    int result;

    if (argc > 1) return run_child(argc, argv) ? 0 : 1;
    if (mkdtemp(process_directory) == NULL) {
        perror("mkdtemp");
        return 1;
    }

    result = test_known_quest_encoding();
    if (result == 0) result = test_pre_fix_raw_wire_loss();
    if (result == 0) result = test_checksum();
    if (result == 0) {
        result = test_process_restart_roundtrip(argv[0], process_directory);
    }
    rmdir(process_directory);
    if (result == 0) puts("Save_t codec/checksum/restart round-trip: PASS");
    return result;
}
