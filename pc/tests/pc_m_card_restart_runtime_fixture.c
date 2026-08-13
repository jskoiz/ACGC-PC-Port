/*
 * Focused game-owned save/restart gate.
 *
 * The initial GCI is seeded only as a valid card image.  The persistence
 * operation under test is the production aNRST_save caller below; replacing
 * that call with pc_m_card_test_write_gci would turn this into an adapter-only
 * test and would not prove the game request path.
 */
#define _POSIX_C_SOURCE 200809L

#include "m_common_data.h"
#include "m_card.h"
#include "m_flashrom.h"
#include "m_msg.h"
#include "m_actor.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char* mkdtemp(char* template_name);
extern void bzero(void* dst, size_t size);

/* Pull the production reload and restart caller into this test translation
 * unit so main() can invoke the actual static aNRST_save state. */
#include "src/game/m_common_data.c"
#include "src/actor/npc/ac_npc_restart.c"

extern int pc_save_loaded;
extern int pc_m_card_test_write_gci(const char* gci_path, const char* tmp_path);

/* Focused link stubs for restart UI/actor services.  None is on the save
 * request's data path; they only let the production caller compile without a
 * full game link. */
void Actor_info_save_actor(GAME_PLAY* play) { (void)play; }
void OSReport(const char* fmt, ...) { (void)fmt; }
int mChoice_Get_ChoseNum(mChoice_c* choice) { (void)choice; return 0; }
mChoice_c* mChoice_Get_base_window_p(void) { return NULL; }
u16 mDemo_Get_OrderValue(int type, int idx) { (void)type; (void)idx; return 0; }
void mDemo_Set_OrderValue(int type, int idx, u16 value) {
    (void)type;
    (void)idx;
    (void)value;
}
int mLd_CheckId(u16 land_id) { (void)land_id; return TRUE; }
int mMsg_Check_MainNormalContinue(mMsg_Window_c* msg) { (void)msg; return FALSE; }
void mMsg_Set_free_str(mMsg_Window_c* msg, int str_no, u8* str, int str_len) {
    (void)msg;
    (void)str_no;
    (void)str;
    (void)str_len;
}
void mNtc_set_auto_nwrite_data(void) {}
void mString_Load_StringFromRom(u8* dst, int dst_len, int str_no) {
    (void)str_no;
    memset(dst, 0, (size_t)dst_len);
}
int pc_card_scan_for_gci(s32 chan, char* out_path, int out_size) {
    (void)chan;
    (void)out_path;
    (void)out_size;
    return FALSE;
}

void mCkRh_SavePlayTime(int player_no) { (void)player_no; }
void mAGrw_ClearMoneyStoneShineGround(void) {}
void mPr_SetPossessionItem(Private_c* priv, int idx, mActor_name_t item, u32 cond) {
    (void)priv;
    (void)idx;
    (void)item;
    (void)cond;
}
OSTime lbRTC_HardTime(void) { return 0; }
f32 fqrand(void) { return 0.5f; }
void mem_clear(u8* dst, size_t size, u8 value) { memset(dst, value, size); }
void mMl_clear_mail(Mail_c* mail) { memset(mail, 0, sizeof(*mail)); }

void mFRm_ClearSaveCheckData(mFRm_chk_t* check) {
    memset(check, 0, sizeof(*check));
    check->code = -1;
    check->land_id = 0xFFFF;
}
void mFRm_SetSaveCheckData(mFRm_chk_t* check) {
    check->code = mFRm_SAVE_ID;
    check->land_id = Save_Get(land_info).id;
}
int mFRm_CheckSaveData(void) { return TRUE; }
u16 mFRm_GetFlatCheckSum(u16* data, int size, u16 old_checksum) {
    u32 sum = 0;
    int i;

    for (i = 0; i < size / 2; i++) sum += data[i];
    return (u16)(0 - (u16)(sum - old_checksum));
}

int none_proc1(void) { return 0; }
int mNpc_GetNpcLooks(ACTOR* actor) { (void)actor; return 0; }
mMsg_Window_c* mMsg_Get_base_window_p(void) { return NULL; }
void mMsg_Set_continue_msg_num(mMsg_Window_c* msg, int no) { (void)msg; (void)no; }
void mMsg_Unset_LockContinue(mMsg_Window_c* msg) { (void)msg; }
void mMsg_Set_LockContinue(mMsg_Window_c* msg) { (void)msg; }

static void prepare_valid_card_save(uint32_t marker) {
    memset(&common_data, 0, sizeof(common_data));
    Save_Set(land_info.id, 0x3234);
    Save_Set(scene_no, (int)marker);
    Save_Set(save_check.version, mFRm_VERSION);
    Save_Set(save_check.code, mFRm_SAVE_ID);
    Save_Set(save_check.land_id, Save_Get(land_info).id);
    Save_Set(save_exist, TRUE);
}

static int read_scene_marker(const char* path, uint32_t* marker) {
    FILE* fp = fopen(path, "rb");
    unsigned char header[sizeof(CARDDir)];
    unsigned char data[sizeof(uint32_t)];
    long offset = (long)sizeof(CARDDir) + 0x26000L +
                  (long)offsetof(Save_t, scene_no);

    if (fp == NULL) return FALSE;
    if (fread(header, sizeof(header), 1, fp) != 1 ||
        fseek(fp, offset, SEEK_SET) != 0 ||
        fread(data, sizeof(data), 1, fp) != 1) {
        fclose(fp);
        return FALSE;
    }
    fclose(fp);

    *marker = ((uint32_t)data[0] << 24) |
              ((uint32_t)data[1] << 16) |
              ((uint32_t)data[2] << 8) |
              (uint32_t)data[3];
    return TRUE;
}

static int child_reload(const char* directory, uint32_t expected) {
    if (chdir(directory) != 0) return FALSE;

    memset(&common_data, 0, sizeof(common_data));
    pc_save_loaded = TRUE;
    common_data_reinit();
    return (uint32_t)Save_Get(scene_no) == expected;
}

int main(int argc, char** argv) {
    char temp_dir[] = "/private/tmp/acgc-save-runtime-XXXXXX";
    char executable[PATH_MAX];
    uint32_t marker;
    NPC_RESTART_ACTOR actor;
    pid_t child;
    int status;

    if (argc == 3 && strcmp(argv[1], "reload") == 0) {
        return child_reload(argv[2], 0x22222222u) ? 0 : 1;
    }
    if (argc != 1 || realpath(argv[0], executable) == NULL) return 1;
    if (mkdtemp(temp_dir) == NULL || chdir(temp_dir) != 0) return 1;
    if (mkdir("save", 0755) != 0 ||
        mkdir("save/card_a", 0755) != 0 ||
        mkdir("save/card_b", 0755) != 0) {
        return 1;
    }

    /* Establish one valid on-card image; this is not the save request under
     * test and is intentionally kept separate from the caller assertion. */
    prepare_valid_card_save(0x11111111u);
    if (!pc_m_card_test_write_gci("save/card_a/DobutsunomoriP_MURA.gci",
                                  "save/card_a/DobutsunomoriP_MURA.gci.tmp")) {
        return 1;
    }

    /* Game-owned state changes, then the real restart caller requests save. */
    Save_Set(scene_no, 0x22222222);
    memset(&actor, 0, sizeof(actor));
    actor.think_idx = aNRST_THINK_TITLE;
    actor.card_chan = -1;
    aNRST_save(&actor, NULL);
    if (actor.card_chan != mCD_SLOT_A || actor.talk_idx != aNRST_TALK_WAIT_END) return 1;
    if (!read_scene_marker("save/card_a/DobutsunomoriP_MURA.gci", &marker) ||
        marker != 0x22222222u) {
        return 1;
    }

    child = fork();
    if (child == 0) {
        execl(executable, executable, "reload", temp_dir, (char*)NULL);
        _exit(127);
    }
    if (child < 0 || waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return 1;
    }

    puts("game-owned restart caller -> GCI marker -> "
         "common_data_reinit fresh-process reload: PASS");
    return 0;
}
