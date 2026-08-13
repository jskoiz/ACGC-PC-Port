#include <stdio.h>
#include <string.h>

#include "dolphin/pad.h"
#include "libultra/osMesg.h"
#include "m_debug.h"
#include "padmgr.h"

u32 pc_frame_counter;
int g_pc_paused;
int g_pc_pause_input_drain;

Debug_mode debug_mode_storage;
Debug_mode* debug_mode = &debug_mode_storage;

static PADStatus injected_status;
static unsigned pad_read_count;

static int fail(const char* message) {
    fprintf(stderr, "pc padmgr frame guard: %s\n", message);
    return 1;
}

#define CHECK(condition, message) \
    do {                              \
        if (!(condition)) {           \
            return fail(message);     \
        }                             \
    } while (0)

/* The fixture owns the input seam; production padmgr.c remains unchanged. */
u32 PADRead(PADStatus* status) {
    memset(status, 0, sizeof(PADStatus) * PAD_MAX_CONTROLLERS);
    status[0] = injected_status;
    pad_read_count++;
    return PAD_CHAN0_BIT;
}

void JW_JUTGamePad_read(void) { }

int osRecvMesg(OSMessageQueue* queue, OSMessage* message, int flags) {
    (void)queue;
    (void)message;
    (void)flags;
    return 0;
}

int osSendMesg(OSMessageQueue* queue, OSMessage message, int flags) {
    (void)queue;
    (void)message;
    (void)flags;
    return 0;
}

static void inject_status(u16 buttons, s8 stick_x, s8 stick_y) {
    memset(&injected_status, 0, sizeof(injected_status));
    injected_status.button = buttons;
    injected_status.stickX = stick_x;
    injected_status.stickY = stick_y;
}

static int pad_matches(const pad_t* actual, const pad_t* expected, const char* label) {
    if (memcmp(actual, expected, sizeof(*actual)) != 0) {
        fprintf(stderr,
                "pc padmgr frame guard: %s mismatch "
                "last=(0x%04x,%d,%d) now=(0x%04x,%d,%d) "
                "on=(0x%04x,%d,%d) off=(0x%04x,%d,%d)\n",
                label,
                actual->last.button, actual->last.stick_x, actual->last.stick_y,
                actual->now.button, actual->now.stick_x, actual->now.stick_y,
                actual->on.button, actual->on.stick_x, actual->on.stick_y,
                actual->off.button, actual->off.stick_x, actual->off.stick_y);
        return 0;
    }
    return 1;
}

int main(void) {
    pad_t game_pads[MAXCONTROLLERS];
    pad_t expected_first;
    pad_t expected_next;
    pad_t first_frame;

    memset(&padmgr_class, 0, sizeof(padmgr_class));
    memset(game_pads, 0, sizeof(game_pads));
    memset(&debug_mode_storage, 0, sizeof(debug_mode_storage));
    pc_frame_counter = 17;
    g_pc_paused = 0;
    g_pc_pause_input_drain = 0;
    pad_read_count = 0;
    padmgr_class.num_controllers = 1;
    padmgr_class.device_type[0] = PADMGR_TYPE_CONTROLLER;

    /* Frame N: a deterministic A press and stick sample become game-owned state. */
    inject_status(PAD_BUTTON_A, 36, -24);
    padmgr_RequestPadData(game_pads, 1);

    memset(&expected_first, 0, sizeof(expected_first));
    expected_first.now.button = 0x8000; /* GC A -> N64 A in padmgr_UpdatePC. */
    expected_first.now.stick_x = 36;
    expected_first.now.stick_y = -24;
    expected_first.on.button = 0x8000;
    expected_first.on.stick_x = 36;
    expected_first.on.stick_y = -24;
    expected_first.off.stick_x = 30; /* (36 * 60) / 72 */
    expected_first.off.stick_y = -20; /* (-24 * 60) / 72 */
    CHECK(pad_matches(&game_pads[0], &expected_first, "first frame"),
          "first frame did not expose the expected game-owned pad state");
    CHECK(pad_read_count == 1, "first request did not perform exactly one PADRead");
    first_frame = game_pads[0];

    /* Same frame: even a changed injected sample must not overwrite the game pad. */
    inject_status(PAD_BUTTON_B, -64, 24);
    padmgr_RequestPadData(game_pads, 1);
    CHECK(pad_read_count == 1, "same-frame request performed a second PADRead");
    CHECK(pad_matches(&game_pads[0], &first_frame, "same-frame second request"),
          "same-frame request changed game-owned now/on/off/last");

    /* Frame N+1: release A; now/last and on/off must advance exactly once. */
    pc_frame_counter++;
    inject_status(0, 0, 0);
    padmgr_RequestPadData(game_pads, 1);

    memset(&expected_next, 0, sizeof(expected_next));
    expected_next.last = expected_first.now;
    expected_next.on.stick_x = -36;
    expected_next.on.stick_y = 24;
    expected_next.off.button = 0x8000;
    CHECK(pad_matches(&game_pads[0], &expected_next, "next frame"),
          "next frame did not advance game-owned now/on/off/last");
    CHECK(pad_read_count == 2, "next-frame request did not perform exactly one new PADRead");

    puts("PC padmgr frame guard: same-frame state preserved; next-frame state advanced");
    return 0;
}
