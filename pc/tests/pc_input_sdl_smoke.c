#include <SDL.h>

#include "pc_keybindings.h"
#include "pc_settings.h"
#include <dolphin/pad.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The SDL input boundary is the only pc_pad.c dependency on these globals. */
int g_pc_typing_mode = 0;
int g_pc_editor_active = 0;
PCKeybindings g_pc_keybindings;
PCPadBindings g_pc_padbindings;
PCSettings g_pc_settings;

void PADCleanup(void);

static int fail(const char* message) {
    fprintf(stderr, "pc SDL input smoke: %s\n", message);
    return 1;
}

static int expect_int(const char* label, int actual, int expected) {
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "pc SDL input smoke: %s: expected %d, got %d\n",
            label, expected, actual);
    return 1;
}

static int skip_with_error(const char* message) {
    fprintf(stderr, "pc SDL input smoke: SKIP: %s (%s)\n",
            message, SDL_GetError());
    return 77;
}

static int host_timeout_ms(void) {
    const char* value = getenv("PC_INPUT_HOST_TIMEOUT_MS");
    if (value == NULL || *value == '\0') {
        return 15000;
    }

    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < 1000 || parsed > 60000) {
        fprintf(stderr,
                "pc SDL input smoke: invalid PC_INPUT_HOST_TIMEOUT_MS=%s; using 15000ms\n",
                value);
        return 15000;
    }
    return (int)parsed;
}

static void set_test_bindings(void) {
    g_pc_keybindings = (PCKeybindings){0};
    g_pc_padbindings = (PCPadBindings){
        .a = SDL_CONTROLLER_BUTTON_A,
        .b = PC_PAD_NONE,
        .x = PC_PAD_NONE,
        .y = PC_PAD_NONE,
        .start = PC_PAD_NONE,
        .z = PC_PAD_NONE,
        .l = PC_PAD_AXIS_BIT | SDL_CONTROLLER_AXIS_TRIGGERLEFT,
        .r = PC_PAD_AXIS_BIT | SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
        .dpad_up = PC_PAD_NONE,
        .dpad_down = PC_PAD_NONE,
        .dpad_left = PC_PAD_NONE,
        .dpad_right = PC_PAD_NONE,
    };
    g_pc_settings = (PCSettings){
        .stick_deadzone = 0,
        .cstick_deadzone = 0,
    };
}

static void set_host_keyboard_bindings(void) {
    /* Keep every non-probed keyboard binding on an unlikely scancode so a
     * human pressing SPACE cannot accidentally satisfy another field. */
    g_pc_keybindings = (PCKeybindings){
        .a = SDL_SCANCODE_SPACE,
        .b = SDL_SCANCODE_F24,
        .x = SDL_SCANCODE_F24,
        .y = SDL_SCANCODE_F24,
        .start = SDL_SCANCODE_F24,
        .z = SDL_SCANCODE_F24,
        .l = SDL_SCANCODE_F24,
        .r = SDL_SCANCODE_F24,
        .stick_up = SDL_SCANCODE_F24,
        .stick_down = SDL_SCANCODE_F24,
        .stick_left = SDL_SCANCODE_F24,
        .stick_right = SDL_SCANCODE_F24,
        .cstick_up = SDL_SCANCODE_F24,
        .cstick_down = SDL_SCANCODE_F24,
        .cstick_left = SDL_SCANCODE_F24,
        .cstick_right = SDL_SCANCODE_F24,
        .dpad_up = SDL_SCANCODE_F24,
        .dpad_down = SDL_SCANCODE_F24,
        .dpad_left = SDL_SCANCODE_F24,
        .dpad_right = SDL_SCANCODE_F24,
    };
    g_pc_padbindings = (PCPadBindings){
        .a = PC_PAD_NONE,
        .b = PC_PAD_NONE,
        .x = PC_PAD_NONE,
        .y = PC_PAD_NONE,
        .start = PC_PAD_NONE,
        .z = PC_PAD_NONE,
        .l = PC_PAD_NONE,
        .r = PC_PAD_NONE,
        .dpad_up = PC_PAD_NONE,
        .dpad_down = PC_PAD_NONE,
        .dpad_left = PC_PAD_NONE,
        .dpad_right = PC_PAD_NONE,
    };
    g_pc_settings = (PCSettings){
        .stick_deadzone = 0,
        .cstick_deadzone = 0,
    };
}

static void set_host_controller_bindings(void) {
    g_pc_keybindings = (PCKeybindings){
        .a = SDL_SCANCODE_F24,
        .b = SDL_SCANCODE_F24,
        .x = SDL_SCANCODE_F24,
        .y = SDL_SCANCODE_F24,
        .start = SDL_SCANCODE_F24,
        .z = SDL_SCANCODE_F24,
        .l = SDL_SCANCODE_F24,
        .r = SDL_SCANCODE_F24,
        .stick_up = SDL_SCANCODE_F24,
        .stick_down = SDL_SCANCODE_F24,
        .stick_left = SDL_SCANCODE_F24,
        .stick_right = SDL_SCANCODE_F24,
        .cstick_up = SDL_SCANCODE_F24,
        .cstick_down = SDL_SCANCODE_F24,
        .cstick_left = SDL_SCANCODE_F24,
        .cstick_right = SDL_SCANCODE_F24,
        .dpad_up = SDL_SCANCODE_F24,
        .dpad_down = SDL_SCANCODE_F24,
        .dpad_left = SDL_SCANCODE_F24,
        .dpad_right = SDL_SCANCODE_F24,
    };
    g_pc_padbindings = (PCPadBindings){
        .a = SDL_CONTROLLER_BUTTON_A,
        .b = PC_PAD_NONE,
        .x = PC_PAD_NONE,
        .y = PC_PAD_NONE,
        .start = PC_PAD_NONE,
        .z = PC_PAD_NONE,
        .l = PC_PAD_NONE,
        .r = PC_PAD_NONE,
        .dpad_up = PC_PAD_NONE,
        .dpad_down = PC_PAD_NONE,
        .dpad_left = PC_PAD_NONE,
        .dpad_right = PC_PAD_NONE,
    };
    g_pc_settings = (PCSettings){
        .stick_deadzone = 0,
        .cstick_deadzone = 0,
    };
}

static int run_host_phase(SDL_Window* window, int controller_phase, int timeout_ms) {
    const char* phase_name = controller_phase ? "controller" : "keyboard";
    const Uint32 start = SDL_GetTicks();
    const Uint32 timeout = (Uint32)timeout_ms;
    int saw_matching_event = 0;

    if (controller_phase) {
        set_host_controller_bindings();
        SDL_SetWindowTitle(window, "ACGC host input probe - press controller A");
        puts("HOST CONTROLLER: press and hold the physical controller A button until PASS");
    } else {
        set_host_keyboard_bindings();
        SDL_SetWindowTitle(window, "ACGC host input probe - press SPACE");
        puts("HOST KEYBOARD: press and hold the physical keyboard SPACE key until PASS");
    }
    SDL_RaiseWindow(window);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);

    for (;;) {
        const Uint32 elapsed = SDL_GetTicks() - start;
        if (elapsed >= timeout) {
            break;
        }

        SDL_Event event;
        int matched_event = 0;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                fprintf(stderr,
                        "pc SDL input smoke: HOST %s skipped because the probe window was closed\n",
                        phase_name);
                return 77;
            }

            if (!controller_phase && event.type == SDL_KEYDOWN &&
                event.key.keysym.scancode == SDL_SCANCODE_SPACE && !event.key.repeat) {
                saw_matching_event = 1;
                matched_event = 1;
                printf("HOST KEYBOARD: SDL_KEYDOWN Space observed (no SDL_PushEvent)\n");
                break;
            }

            if (controller_phase && event.type == SDL_CONTROLLERBUTTONDOWN &&
                event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                saw_matching_event = 1;
                matched_event = 1;
                printf("HOST CONTROLLER: SDL_CONTROLLERBUTTONDOWN A observed (no SDL_PushEvent)\n");
                break;
            }
        }

        if (matched_event) {
            PADStatus status[PAD_MAX_CONTROLLERS];
            memset(status, 0xA5, sizeof(status));
            const u32 channel_mask = PADRead(status);
            const int reached = (channel_mask & PAD_CHAN0_BIT) != 0 &&
                (status[0].button & PAD_BUTTON_A) != 0;
            if (reached) {
                printf("HOST %s: OS event -> PADRead/logical snapshot PASS "
                       "channel=0x%08x buttons=0x%04x stick=(%d,%d)\n",
                       controller_phase ? "CONTROLLER" : "KEYBOARD",
                       channel_mask, status[0].button,
                       status[0].stickX, status[0].stickY);
                return 0;
            }

            fprintf(stderr,
                    "pc SDL input smoke: HOST %s event arrived but PADRead did not expose A "
                    "(channel=0x%08x buttons=0x%04x)\n",
                    phase_name, channel_mask, status[0].button);
            return 1;
        }

        SDL_Delay(8);
    }

    if (saw_matching_event) {
        fprintf(stderr,
                "pc SDL input smoke: HOST %s event was observed, but no pressed-state sample completed\n",
                phase_name);
        return 1;
    }

    fprintf(stderr,
            "pc SDL input smoke: HOST %s SKIP after %dms; blocker: no physical %s event "
            "reached the focused SDL window\n",
            phase_name, timeout_ms, controller_phase ? "controller-A" : "keyboard-Space");
    return 77;
}

static int run_host_input_path(void) {
    const int timeout_ms = host_timeout_ms();
    SDL_Window* window = SDL_CreateWindow(
        "ACGC host input probe",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        800,
        240,
        SDL_WINDOW_SHOWN
    );
    if (window == NULL) {
        return skip_with_error("host probe window could not be created");
    }

    if (!PADInit()) {
        SDL_DestroyWindow(window);
        return fail("host PADInit returned FALSE");
    }

    int keyboard_result = run_host_phase(window, 0, timeout_ms);
    int controller_result = run_host_phase(window, 1, timeout_ms);
    PADCleanup();
    SDL_DestroyWindow(window);

    if (keyboard_result == 1 || controller_result == 1) {
        return 1;
    }
    if (keyboard_result == 77 || controller_result == 77) {
        return 77;
    }
    puts("HOST INPUT: physical keyboard and controller paths reached PADRead");
    return 0;
}

static int set_virtual_controller_state(
    SDL_Joystick* joystick,
    Sint16 left_trigger,
    Sint16 right_trigger,
    Uint8 left_shoulder,
    Uint8 right_shoulder
) {
    return SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_A, 1) < 0 ||
        SDL_JoystickSetVirtualButton(
            joystick, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, left_shoulder
        ) < 0 ||
        SDL_JoystickSetVirtualButton(
            joystick, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, right_shoulder
        ) < 0 ||
        SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, 16384) < 0 ||
        SDL_JoystickSetVirtualAxis(
            joystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT,
            (Sint16)(2 * (int)left_trigger - 32768)
        ) < 0 ||
        SDL_JoystickSetVirtualAxis(
            joystick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
            (Sint16)(2 * (int)right_trigger - 32768)
        ) < 0;
}

static int sample_virtual_controller(
    const char* label,
    PADStatus* status,
    u32* channel_mask
) {
    PADStatus repeated[PAD_MAX_CONTROLLERS];
    const size_t status_size = sizeof(PADStatus) * PAD_MAX_CONTROLLERS;

    SDL_JoystickUpdate();
    SDL_GameControllerUpdate();

    memset(status, 0xA5, status_size);
    *channel_mask = PADRead(status);
    memset(repeated, 0x3C, sizeof(repeated));
    const u32 repeated_channel_mask = PADRead(repeated);
    if (repeated_channel_mask != *channel_mask ||
        memcmp(status, repeated, sizeof(repeated)) != 0) {
        fprintf(stderr,
                "pc SDL input smoke: consecutive PADRead samples differed for %s "
                "(mask1=0x%08x mask2=0x%08x)\n",
                label, (unsigned)*channel_mask, (unsigned)repeated_channel_mask);
        return 1;
    }

    printf("DOUBLE_PADREAD state=%s mask1=0x%08x mask2=0x%08x identical=1 "
           "buttons=0x%04x triggers=(%u,%u)\n",
           label, (unsigned)*channel_mask, (unsigned)repeated_channel_mask,
           status[0].button, (unsigned)status[0].triggerLeft,
           (unsigned)status[0].triggerRight);
    return 0;
}

static int expect_virtual_trigger_state(
    const char* label,
    u32 channel_mask,
    const PADStatus* status,
    u16 expected_buttons,
    int expected_left_trigger,
    int expected_right_trigger
) {
    int failures = 0;
    if (channel_mask != PAD_CHAN0_BIT) {
        fprintf(stderr,
                "pc SDL input smoke: %s channel mask: expected 0x%08x, got 0x%08x\n",
                label, (unsigned)PAD_CHAN0_BIT, (unsigned)channel_mask);
        failures++;
    }
    if (status[0].button != expected_buttons) {
        fprintf(stderr,
                "pc SDL input smoke: %s buttons: expected 0x%04x, got 0x%04x\n",
                label, expected_buttons, status[0].button);
        failures++;
    }
    if (status[0].triggerLeft != expected_left_trigger) {
        fprintf(stderr,
                "pc SDL input smoke: %s left trigger: expected %d, got %d\n",
                label, expected_left_trigger, status[0].triggerLeft);
        failures++;
    }
    if (status[0].triggerRight != expected_right_trigger) {
        fprintf(stderr,
                "pc SDL input smoke: %s right trigger: expected %d, got %d\n",
                label, expected_right_trigger, status[0].triggerRight);
        failures++;
    }
    return failures;
}

static int run_virtual_controller_path(void) {
    const Sint16 subthreshold_left_trigger = (Sint16)((88 << 7) + 1);
    const Sint16 above_threshold_left_trigger = 20000;
    const Sint16 above_threshold_right_trigger = 25000;
    const int virtual_index = SDL_JoystickAttachVirtual(
        SDL_JOYSTICK_TYPE_GAMECONTROLLER,
        6,
        SDL_CONTROLLER_BUTTON_MAX,
        0
    );
    if (virtual_index < 0) {
        return skip_with_error("SDL virtual game controller is unavailable");
    }
    if (!SDL_IsGameController(virtual_index)) {
        SDL_JoystickDetachVirtual(virtual_index);
        return skip_with_error("SDL virtual joystick has no game-controller mapping");
    }

    SDL_Joystick* joystick = SDL_JoystickOpen(virtual_index);
    if (joystick == NULL) {
        SDL_JoystickDetachVirtual(virtual_index);
        return skip_with_error("SDL virtual game controller could not be opened");
    }

    set_test_bindings();
    if (!PADInit()) {
        SDL_JoystickClose(joystick);
        SDL_JoystickDetachVirtual(virtual_index);
        return fail("PADInit returned FALSE");
    }

    SDL_GameController* controller = SDL_GameControllerFromInstanceID(
        SDL_JoystickInstanceID(joystick)
    );
    if (controller == NULL) {
        PADCleanup();
        SDL_JoystickClose(joystick);
        SDL_JoystickDetachVirtual(virtual_index);
        return fail("PADInit did not open the virtual controller instance");
    }

    if (set_virtual_controller_state(joystick, 0, 0, 0, 0) < 0) {
        PADCleanup();
        SDL_JoystickClose(joystick);
        SDL_JoystickDetachVirtual(virtual_index);
        return fail("SDL virtual controller state could not be set");
    }

    PADStatus status[PAD_MAX_CONTROLLERS];
    u32 channel_mask = 0;
    int failures = 0;

    failures += sample_virtual_controller("axis-zero", status, &channel_mask);
    failures += expect_virtual_trigger_state(
        "axis-zero", channel_mask, status, PAD_BUTTON_A, 0, 0
    );
    failures += expect_int("left stick mapping", status[0].stickX, 64);

    if (set_virtual_controller_state(
            joystick, subthreshold_left_trigger, 0, 0, 0
        ) < 0) {
        failures += fail("SDL virtual sub-threshold trigger state could not be set");
        goto cleanup;
    }
    failures += sample_virtual_controller(
        "axis-subthreshold-88", status, &channel_mask
    );
    failures += expect_virtual_trigger_state(
        "axis-subthreshold-88", channel_mask, status,
        PAD_BUTTON_A | PAD_TRIGGER_L, 88, 0
    );

    if (set_virtual_controller_state(
            joystick,
            above_threshold_left_trigger,
            above_threshold_right_trigger,
            0,
            0
        ) < 0) {
        failures += fail("SDL virtual above-threshold trigger state could not be set");
        goto cleanup;
    }
    failures += sample_virtual_controller(
        "axis-above-threshold", status, &channel_mask
    );
    failures += expect_virtual_trigger_state(
        "axis-above-threshold", channel_mask, status,
        PAD_BUTTON_A | PAD_TRIGGER_L | PAD_TRIGGER_R,
        above_threshold_left_trigger >> 7,
        above_threshold_right_trigger >> 7
    );

    g_pc_padbindings.l = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    g_pc_padbindings.r = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    if (set_virtual_controller_state(
            joystick, above_threshold_left_trigger, above_threshold_right_trigger, 0, 0
        ) < 0) {
        failures += fail("SDL virtual digital-trigger release state could not be set");
        goto cleanup;
    }
    failures += sample_virtual_controller(
        "digital-binding-axis-only", status, &channel_mask
    );
    failures += expect_virtual_trigger_state(
        "digital-binding-axis-only", channel_mask, status, PAD_BUTTON_A, 0, 0
    );

    if (set_virtual_controller_state(joystick, 0, 0, 1, 1) < 0) {
        failures += fail("SDL virtual digital-trigger press state could not be set");
        goto cleanup;
    }
    failures += sample_virtual_controller(
        "digital-binding-pressed", status, &channel_mask
    );
    failures += expect_virtual_trigger_state(
        "digital-binding-pressed", channel_mask, status,
        PAD_BUTTON_A | PAD_TRIGGER_L | PAD_TRIGGER_R, 255, 255
    );

cleanup:
    PADCleanup();
    SDL_JoystickClose(joystick);
    SDL_JoystickDetachVirtual(virtual_index);

    if (failures != 0) {
        return failures;
    }
    puts("SDL controller path: virtual joystick -> PADInit/PADRead passed");
    return 0;
}

static int run_synthetic_keyboard_path(void) {
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    SDL_ResetKeyboard();
    SDL_PumpEvents();

    int num_keys = 0;
    const Uint8* before = SDL_GetKeyboardState(&num_keys);
    if (num_keys <= SDL_SCANCODE_SPACE || before[SDL_SCANCODE_SPACE] != 0) {
        puts("SDL keyboard path: no isolated synthetic claim; SPACE was already held");
        return 0;
    }

    SDL_Event pushed = {0};
    pushed.type = SDL_KEYDOWN;
    pushed.key.state = SDL_PRESSED;
    pushed.key.keysym.scancode = SDL_SCANCODE_SPACE;
    pushed.key.keysym.sym = SDLK_SPACE;
    if (SDL_PushEvent(&pushed) < 0) {
        return fail("SDL_PushEvent(SDL_KEYDOWN) failed");
    }

    SDL_Event observed = {0};
    if (SDL_PollEvent(&observed) != 1 ||
        observed.type != SDL_KEYDOWN ||
        observed.key.keysym.scancode != SDL_SCANCODE_SPACE) {
        return fail("SDL_PollEvent did not deliver the synthetic key event");
    }

    SDL_PumpEvents();
    const Uint8* after = SDL_GetKeyboardState(NULL);
    if (after[SDL_SCANCODE_SPACE] != 0) {
        g_pc_keybindings = (PCKeybindings){0};
        g_pc_keybindings.a = SDL_SCANCODE_SPACE;
        PADStatus status[PAD_MAX_CONTROLLERS];
        memset(status, 0, sizeof(status));
        const u32 channel_mask = PADRead(status);
        if ((channel_mask & PAD_CHAN0_BIT) == 0 ||
            (status[0].button & PAD_BUTTON_A) == 0) {
            return fail("SDL keyboard state did not reach PADRead");
        }
        puts("SDL keyboard path: synthetic key updated SDL_GetKeyboardState and PADRead");
        return 0;
    }

    puts("SDL keyboard path: SDL_PushEvent delivered KEYDOWN, but SDL_GetKeyboardState stayed released");
    puts("SDL keyboard limitation: an OS/human keyboard event is required for PADRead state proof");
    return 0;
}

int main(int argc, char** argv) {
    const int host_mode = argc > 1 && strcmp(argv[1], "--host") == 0;
    if (!host_mode) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        return skip_with_error("SDL dummy/video-safe initialization failed");
    }

    int result;
    if (host_mode) {
        result = run_host_input_path();
    } else {
        result = run_virtual_controller_path();
        if (result == 0) {
            result = run_synthetic_keyboard_path();
        }
    }

    SDL_Quit();
    return result;
}
