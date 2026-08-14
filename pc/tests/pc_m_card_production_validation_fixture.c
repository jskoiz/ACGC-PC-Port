#define _POSIX_C_SOURCE 200809L

#include "m_common_data.h"
#include "m_flashrom.h"
#include "pc_save_bswap.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The decomp's host headers intentionally shadow the system stdlib header. */
extern char* mkdtemp(char* template_name);
extern char* realpath(const char* path, char* resolved_path);

enum {
    GCI_HEADER_SIZE = 64,
    GCI_FILE_DATA_SIZE = 0x72000,
    GCI_SAVE_MAIN_OFFSET = 0x26000,
    GCI_SAVE_BACK_OFFSET = 0x4C000,
    GCI_SECTOR_SIZE = 0x2000,
    SAVE_TAIL_CHECK_BYTES = 32,
};

static const char* const GCI_PATH = "save/card_a/DobutsunomoriP_MURA.gci";
static const char* const GCI_TMP_PATH = "save/card_a/DobutsunomoriP_MURA.gci.tmp";

_Static_assert(sizeof(CARDDir) == GCI_HEADER_SIZE, "CARDDir wire size changed");
_Static_assert(sizeof(Save_t) == 0x242A0, "Save_t wire size changed");
_Static_assert(sizeof(Save) == 0x26000, "aligned Save wire size changed");
_Static_assert(sizeof(Save) - sizeof(Save_t) >= SAVE_TAIL_CHECK_BYTES,
               "Save alignment tail is too small for this fixture");

common_data_t common_data;

extern int pc_m_card_test_write_gci(const char* gci_path, const char* tmp_path);
extern int pc_m_card_test_check_and_load(void);

void mCkRh_SavePlayTime(int player_no) {
    (void)player_no;
}

void mAGrw_ClearMoneyStoneShineGround(void) {}

void mPr_SetPossessionItem(Private_c* priv, int idx, mActor_name_t item,
                           u32 cond) {
    (void)priv;
    (void)idx;
    (void)item;
    (void)cond;
}

OSTime lbRTC_HardTime(void) {
    return 0;
}

f32 fqrand(void) {
    return 0.5f;
}

void mFRm_SetSaveCheckData(mFRm_chk_t* check) {
    check->code = mFRm_SAVE_ID;
    check->land_id = common_data.save.save.land_info.id;
}

int pc_card_scan_for_gci(int chan, char* out_path, int out_size) {
    (void)chan;
    (void)out_path;
    (void)out_size;
    return 0;
}

int mLd_CheckId(u16 land_id) {
    return (land_id & 0x3000) == 0x3000;
}

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
        if (!check_condition((condition), #condition, __LINE__)) goto cleanup; \
    } while (0)

static uint16_t read_be16(const unsigned char* bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static int read_file(const char* path, unsigned char** data_out, size_t* size_out) {
    FILE* file = fopen(path, "rb");
    long file_size;
    unsigned char* data;

    if (file == NULL) return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    file_size = ftell(file);
    if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    data = (unsigned char*)malloc((size_t)file_size);
    if (data == NULL || fread(data, 1, (size_t)file_size, file) != (size_t)file_size) {
        free(data);
        fclose(file);
        return 0;
    }
    fclose(file);
    *data_out = data;
    *size_out = (size_t)file_size;
    return 1;
}

static int corrupt_main_tail_byte(void) {
    FILE* file = fopen(GCI_PATH, "r+b");
    long offset = GCI_HEADER_SIZE + GCI_SAVE_MAIN_OFFSET + sizeof(Save_t) + 7;
    unsigned char byte = 0x00;
    int success = 0;

    if (file != NULL && fseek(file, offset, SEEK_SET) == 0 &&
        fread(&byte, 1, sizeof(byte), file) == sizeof(byte) &&
        fseek(file, offset, SEEK_SET) == 0) {
        byte ^= 0xFF;
        success = fwrite(&byte, 1, sizeof(byte), file) == sizeof(byte) &&
                  fflush(file) == 0;
    }
    if (file != NULL) fclose(file);
    return success;
}

static void prepare_save(void) {
    unsigned char* raw;
    size_t i;

    memset(&common_data, 0, sizeof(common_data));
    common_data.save.save.land_info.id = 0x3234;
    common_data.save.save.scene_no = 0x55667788;
    common_data.save.save.save_check.version = mFRm_VERSION;
    common_data.save.save.save_check.code = mFRm_SAVE_ID;
    common_data.save.save.save_check.land_id = common_data.save.save.land_info.id;
    common_data.save.save.save_exist = TRUE;

    /* The aligned Save tail is part of the decomp CARD wire span. */
    raw = (unsigned char*)&common_data.save;
    for (i = sizeof(Save_t); i < sizeof(Save_t) + SAVE_TAIL_CHECK_BYTES; i++) {
        raw[i] = (unsigned char)(0xA0u + ((i - sizeof(Save_t)) * 3u));
    }
}

static int check_tail(const unsigned char* wire) {
    size_t i;

    for (i = 0; i < SAVE_TAIL_CHECK_BYTES; i++) {
        unsigned char expected = (unsigned char)(0xA0u + i * 3u);
        if (wire[sizeof(Save_t) + i] != expected) return 0;
    }
    return 1;
}

int main(void) {
    char original_cwd[PATH_MAX];
    char temp_dir[] = "/private/tmp/acgc-lane-card-production-validation-fixture-XXXXXX";
    unsigned char* file_data = NULL;
    size_t file_size = 0;
    const unsigned char* main_wire;
    const unsigned char* backup_wire;
    int temp_created = 0;
    int in_temp = 0;
    int result = 1;

    if (getcwd(original_cwd, sizeof(original_cwd)) == NULL) return 1;
    if (mkdtemp(temp_dir) == NULL) return 1;
    temp_created = 1;
    if (chdir(temp_dir) != 0) goto cleanup;
    in_temp = 1;
    CHECK(mkdir("save", 0755) == 0);
    CHECK(mkdir("save/card_a", 0755) == 0);
    CHECK(mkdir("save/card_b", 0755) == 0);

    prepare_save();
    CHECK(pc_m_card_test_write_gci(GCI_PATH, GCI_TMP_PATH));
    CHECK(access(GCI_PATH, F_OK) == 0);
    CHECK(access(GCI_TMP_PATH, F_OK) != 0);
    CHECK(read_file(GCI_PATH, &file_data, &file_size));
    CHECK(file_size == GCI_HEADER_SIZE + GCI_FILE_DATA_SIZE);
    CHECK(memcmp(file_data, "GAFE", 4) == 0);
    CHECK(memcmp(file_data + 4, "01", 2) == 0);
    CHECK(memcmp(file_data + 8, "DobutsunomoriP_MURA", 19) == 0);
    CHECK(read_be16(file_data + 0x36) == 5);
    CHECK(read_be16(file_data + 0x38) == GCI_FILE_DATA_SIZE / GCI_SECTOR_SIZE);

    main_wire = file_data + GCI_HEADER_SIZE + GCI_SAVE_MAIN_OFFSET;
    backup_wire = file_data + GCI_HEADER_SIZE + GCI_SAVE_BACK_OFFSET;
    CHECK(memcmp(main_wire, backup_wire, sizeof(Save)) == 0);
    CHECK(pc_checksum_be(main_wire, sizeof(Save), 0) == 0);
    CHECK(pc_checksum_be(backup_wire, sizeof(Save), 0) == 0);
    CHECK(check_tail(main_wire));

    /* A fresh load must retain the full aligned wire span, not only Save_t. */
    memset(&common_data, 0xCD, sizeof(common_data));
    CHECK(pc_m_card_test_check_and_load());
    CHECK(common_data.save.save.scene_no == 0x55667788);
    CHECK(check_tail((const unsigned char*)&common_data.save));

    free(file_data);
    file_data = NULL;
    CHECK(corrupt_main_tail_byte());
    memset(&common_data, 0, sizeof(common_data));
    CHECK(pc_m_card_test_check_and_load());
    CHECK(common_data.save.save.scene_no == 0x55667788);
    CHECK(check_tail((const unsigned char*)&common_data.save));

    result = 0;

cleanup:
    free(file_data);
    if (in_temp) {
        unlink(GCI_PATH);
        unlink(GCI_TMP_PATH);
        rmdir("save/card_a");
        rmdir("save/card_b");
        rmdir("save");
        if (chdir(original_cwd) != 0) return 1;
    }
    if (temp_created && rmdir(temp_dir) != 0) return 1;
    if (result == 0) puts("production m_card Save wire/checksum/backup validation: PASS");
    return result;
}
