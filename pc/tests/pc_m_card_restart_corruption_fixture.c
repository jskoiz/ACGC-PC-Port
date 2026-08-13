#define _POSIX_C_SOURCE 200809L

#include "m_common_data.h"
#include "m_flashrom.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* The decomp's host headers intentionally shadow the system stdlib header. */
extern char* mkdtemp(char* template_name);
extern char* realpath(const char* path, char* resolved_path);

enum {
    GCI_HEADER_SIZE = 64,
    GCI_FILE_DATA_SIZE = 0x72000,
    GCI_SAVE_MAIN_OFFSET = 0x26000,
    GCI_SAVE_BACK_OFFSET = 0x4C000,
};

static const char* const GCI_PATH = "save/card_a/DobutsunomoriP_MURA.gci";
static const char* const GCI_TMP_PATH = "save/card_a/DobutsunomoriP_MURA.gci.tmp";
static const char* const GCI_BAK1_PATH = "save/card_a/DobutsunomoriP_MURA.gci.bak1";

common_data_t common_data;
extern int pc_save_loaded;

extern int pc_m_card_test_write_gci(const char* gci_path, const char* tmp_path);
extern int pc_m_card_test_check_and_load(void);

/* The production writer calls this game-owned routine before encoding Save_t. */
void mFRm_SetSaveCheckData(mFRm_chk_t* check) {
    check->code = mFRm_SAVE_ID;
    check->land_id = common_data.save.save.land_info.id;
}

/* The production scan path is not part of this fixture's scope. */
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

static uint32_t read_be32(const unsigned char* bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static int read_scene_marker(const char* path, size_t save_offset,
                             uint32_t* marker) {
    FILE* file = fopen(path, "rb");
    unsigned char bytes[sizeof(uint32_t)];
    long offset = GCI_HEADER_SIZE + (long)save_offset +
                  (long)offsetof(Save_t, scene_no);
    int success = 0;

    if (file == NULL) return 0;
    if (fseek(file, offset, SEEK_SET) == 0 &&
        fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes)) {
        *marker = read_be32(bytes);
        success = 1;
    }
    fclose(file);
    return success;
}

static int corrupt_main_scene_marker(void) {
    FILE* file = fopen(GCI_PATH, "r+b");
    unsigned char byte = 0xDE;
    long offset = GCI_HEADER_SIZE + GCI_SAVE_MAIN_OFFSET +
                  (long)offsetof(Save_t, scene_no);
    int success = 0;

    if (file == NULL) return 0;
    if (fseek(file, offset, SEEK_SET) == 0 &&
        fwrite(&byte, 1, sizeof(byte), file) == sizeof(byte) &&
        fflush(file) == 0) {
        success = 1;
    }
    fclose(file);
    return success;
}

static int corrupt_main_header(void) {
    FILE* file = fopen(GCI_PATH, "r+b");
    unsigned char byte = 0;
    int success = 0;

    if (file == NULL) return 0;
    if (fwrite(&byte, 1, sizeof(byte), file) == sizeof(byte) &&
        fflush(file) == 0) {
        success = 1;
    }
    fclose(file);
    return success;
}

static void prepare_save(uint32_t marker) {
    memset(&common_data, 0, sizeof(common_data));
    common_data.save.save.land_info.id = 0x3234;
    common_data.save.save.scene_no = (int)marker;
    common_data.save.save.save_check.version = mFRm_VERSION;
    common_data.save.save.save_check.code = mFRm_SAVE_ID;
    common_data.save.save.save_check.land_id = common_data.save.save.land_info.id;
    common_data.save.save.save_exist = TRUE;
}

static int run_restart_child(const char* executable, const char* directory,
                             uint32_t expected_marker, int expected_result) {
    char marker_text[32];
    char result_text[8];
    pid_t child;
    int status;

    snprintf(marker_text, sizeof(marker_text), "%u", expected_marker);
    snprintf(result_text, sizeof(result_text), "%d", expected_result);
    child = fork();
    if (child < 0) return 0;
    if (child == 0) {
        execl(executable, executable, "restart", directory, marker_text,
              result_text, (char*)NULL);
        _exit(127);
    }
    if (waitpid(child, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int run_restart_mode(int argc, char** argv) {
    char* end;
    uint32_t expected_marker;
    int expected_result;
    int result;

    if (argc != 5 || strcmp(argv[1], "restart") != 0) return 0;
    if (chdir(argv[2]) != 0) return 0;
    expected_marker = (uint32_t)strtoul(argv[3], &end, 10);
    if (*end != '\0') return 0;
    expected_result = (int)strtol(argv[4], &end, 10);
    if (*end != '\0') return 0;

    memset(&common_data, 0, sizeof(common_data));
    pc_save_loaded = 0;
    result = pc_m_card_test_check_and_load();
    if (result != expected_result) return 1;
    if (result && (uint32_t)common_data.save.save.scene_no != expected_marker) {
        fprintf(stderr, "restart loaded marker 0x%08X, expected 0x%08X\n",
                (unsigned int)common_data.save.save.scene_no,
                (unsigned int)expected_marker);
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    char original_cwd[PATH_MAX];
    char temp_dir[] = "/private/tmp/acgc-mcard-restart-corruption-XXXXXX";
    char executable[PATH_MAX];
    uint32_t marker;
    int temp_created = 0;
    int in_temp = 0;
    int result = 1;

    if (argc > 1) return run_restart_mode(argc, argv);
    if (getcwd(original_cwd, sizeof(original_cwd)) == NULL) return 1;
    if (realpath(argv[0], executable) == NULL) return 1;
    if (mkdtemp(temp_dir) == NULL) return 1;
    temp_created = 1;
    if (chdir(temp_dir) != 0) goto cleanup;
    in_temp = 1;
    CHECK(mkdir("save", 0755) == 0);
    CHECK(mkdir("save/card_a", 0755) == 0);
    CHECK(mkdir("save/card_b", 0755) == 0);

    /* Generation 1: the first successful production write has no tmp residue. */
    prepare_save(0x11111111u);
    CHECK(pc_m_card_test_write_gci(GCI_PATH, GCI_TMP_PATH));
    CHECK(access(GCI_PATH, F_OK) == 0);
    CHECK(access(GCI_TMP_PATH, F_OK) != 0);
    CHECK(access(GCI_BAK1_PATH, F_OK) != 0);
    CHECK(read_scene_marker(GCI_PATH, GCI_SAVE_MAIN_OFFSET, &marker));
    CHECK(marker == 0x11111111u);

    /* Generation 2: replacement is complete before the new main is visible. */
    prepare_save(0x22222222u);
    CHECK(pc_m_card_test_write_gci(GCI_PATH, GCI_TMP_PATH));
    CHECK(access(GCI_TMP_PATH, F_OK) != 0);
    CHECK(read_scene_marker(GCI_PATH, GCI_SAVE_MAIN_OFFSET, &marker));
    CHECK(marker == 0x22222222u);
    CHECK(read_scene_marker(GCI_BAK1_PATH, GCI_SAVE_MAIN_OFFSET, &marker));
    CHECK(marker == 0x11111111u);

    /* A new process must reload the latest game-owned Save_t from disk. */
    CHECK(run_restart_child(executable, temp_dir, 0x22222222u, 1));

    /* Corrupt only the main Save_t. The embedded GameCube backup must win. */
    CHECK(corrupt_main_scene_marker());
    CHECK(run_restart_child(executable, temp_dir, 0x22222222u, 1));

    /* If the whole current GCI is unusable, the previous atomic generation is next. */
    CHECK(corrupt_main_header());
    CHECK(run_restart_child(executable, temp_dir, 0x11111111u, 1));

    result = 0;

cleanup:
    if (in_temp) {
        unlink(GCI_PATH);
        unlink(GCI_TMP_PATH);
        unlink(GCI_BAK1_PATH);
        rmdir("save/card_a");
        rmdir("save/card_b");
        rmdir("save");
        if (chdir(original_cwd) != 0) return 1;
    }
    if (temp_created && rmdir(temp_dir) != 0) return 1;
    if (result == 0) puts("production m_card atomic/restart/corruption recovery: PASS");
    return result;
}
