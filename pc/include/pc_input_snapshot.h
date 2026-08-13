#ifndef PC_INPUT_SNAPSHOT_H
#define PC_INPUT_SNAPSHOT_H

#include <stdint.h>
#include <string.h>

#include <dolphin/pad.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host-independent logical controller state.  SDL and its opaque handles
 * stop at this boundary; the field widths match the corresponding GameCube
 * controller values without depending on host ABI-sized aliases.
 */
typedef struct {
    uint16_t buttons;
    int8_t stick_x;
    int8_t stick_y;
    int8_t substick_x;
    int8_t substick_y;
    uint8_t trigger_left;
    uint8_t trigger_right;
} PCInputSnapshot;

#if defined(__cplusplus)
static_assert(sizeof(PCInputSnapshot) == 8, "PCInputSnapshot must remain 8 bytes");
#else
_Static_assert(sizeof(PCInputSnapshot) == 8, "PCInputSnapshot must remain 8 bytes");
#endif

/* Apply one logical snapshot to the native PADStatus channel 0 shape. */
static inline void pc_input_snapshot_to_pad_status(
    const PCInputSnapshot* snapshot,
    PADStatus* status
) {
    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(PADStatus) * PAD_MAX_CONTROLLERS);
    if (snapshot == NULL) {
        return;
    }

    status[0].button = snapshot->buttons;
    status[0].stickX = snapshot->stick_x;
    status[0].stickY = snapshot->stick_y;
    status[0].substickX = snapshot->substick_x;
    status[0].substickY = snapshot->substick_y;
    status[0].triggerLeft = snapshot->trigger_left;
    status[0].triggerRight = snapshot->trigger_right;
    status[0].err = PAD_ERR_NONE;
}

#ifdef __cplusplus
}
#endif

#endif /* PC_INPUT_SNAPSHOT_H */
