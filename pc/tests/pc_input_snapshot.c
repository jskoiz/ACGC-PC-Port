#include "pc_input_snapshot.h"

#include <dolphin/pad.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int expect_int(const char* label, int actual, int expected) {
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "%s: expected %d, got %d\n", label, expected, actual);
    return 1;
}

int main(void) {
    PADStatus status[PAD_MAX_CONTROLLERS];
    const PADStatus zero_status[PAD_MAX_CONTROLLERS] = {0};
    const PCInputSnapshot injected = {
        .buttons = PAD_BUTTON_A | PAD_BUTTON_START | PAD_BUTTON_LEFT,
        .stick_x = INT8_MIN,
        .stick_y = INT8_MAX,
        .substick_x = -37,
        .substick_y = 91,
        .trigger_left = 17,
        .trigger_right = UINT8_MAX,
    };
    int failures = 0;

    memset(status, 0xA5, sizeof(status));
    pc_input_snapshot_to_pad_status(&injected, status);

    failures += expect_int("button bits", status[0].button, injected.buttons);
    failures += expect_int("stick x", status[0].stickX, injected.stick_x);
    failures += expect_int("stick y", status[0].stickY, injected.stick_y);
    failures += expect_int("substick x", status[0].substickX, injected.substick_x);
    failures += expect_int("substick y", status[0].substickY, injected.substick_y);
    failures += expect_int("left trigger", status[0].triggerLeft, injected.trigger_left);
    failures += expect_int("right trigger", status[0].triggerRight, injected.trigger_right);
    failures += expect_int("analog A remains neutral", status[0].analogA, 0);
    failures += expect_int("analog B remains neutral", status[0].analogB, 0);
    failures += expect_int("status error", status[0].err, PAD_ERR_NONE);

    if (memcmp(&status[1], &zero_status[1], sizeof(status) - sizeof(status[0])) != 0) {
        fprintf(stderr, "non-channel-0 statuses were not cleared\n");
        failures++;
    }

    memset(status, 0xA5, sizeof(status));
    pc_input_snapshot_to_pad_status(NULL, status);
    if (memcmp(status, zero_status, sizeof(status)) != 0) {
        fprintf(stderr, "null snapshot was not treated as neutral\n");
        failures++;
    }

    if (failures != 0) {
        return 1;
    }
    puts("pc input snapshot tests passed");
    return 0;
}
