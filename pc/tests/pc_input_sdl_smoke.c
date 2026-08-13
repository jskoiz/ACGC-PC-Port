#include <SDL.h>

#include "pc_keybindings.h"
#include "pc_settings.h"
#include <dolphin/pad.h>

#include <stdio.h>
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

static int run_virtual_controller_path(void) {
    const Sint16 virtual_left_trigger = 20000;
    const Sint16 virtual_right_trigger = 25000;
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

    if (SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_A, 1) < 0 ||
        SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, 16384) < 0 ||
        SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT, virtual_left_trigger) < 0 ||
        SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, virtual_right_trigger) < 0) {
        PADCleanup();
        SDL_JoystickClose(joystick);
        SDL_JoystickDetachVirtual(virtual_index);
        return fail("SDL virtual controller state could not be set");
    }

    SDL_JoystickUpdate();
    SDL_GameControllerUpdate();

    PADStatus status[PAD_MAX_CONTROLLERS];
    memset(status, 0xA5, sizeof(status));
    const u32 channel_mask = PADRead(status);
    const u16 expected_buttons = PAD_BUTTON_A | PAD_TRIGGER_L | PAD_TRIGGER_R;
    const int expected_left_trigger = SDL_GameControllerGetAxis(
        controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT
    ) >> 7;
    const int expected_right_trigger = SDL_GameControllerGetAxis(
        controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT
    ) >> 7;
    int failures = 0;

    failures += expect_int("channel mask", (int)(channel_mask >> 31), 1);
    failures += expect_int("controller button mapping", status[0].button, expected_buttons);
    failures += expect_int("left stick mapping", status[0].stickX, 64);
    failures += expect_int("left trigger value", status[0].triggerLeft, expected_left_trigger);
    failures += expect_int("right trigger value", status[0].triggerRight, expected_right_trigger);

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

int main(void) {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        return skip_with_error("SDL dummy/video-safe initialization failed");
    }

    int result = run_virtual_controller_path();
    if (result == 0) {
        result = run_synthetic_keyboard_path();
    }

    SDL_Quit();
    return result;
}
