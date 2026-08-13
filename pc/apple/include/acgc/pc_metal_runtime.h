#ifndef ACGC_PC_METAL_RUNTIME_H
#define ACGC_PC_METAL_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The PC Apple runtime owns this fixed registration bridge. It records only
 * bounded handoff/status observations and copies the Metal sink's bounded
 * completion/readback observations; it does not retain guest pointers or
 * expose a game-frame claim.
 */
typedef struct AcgcPcMetalRuntimeSnapshot {
    uint32_t registered;
    uint32_t handoff_count;
    uint32_t accepted_count;
    uint32_t rejected_count;
    uint32_t last_status;
    uint32_t sink_initialized;
    uint32_t sink_available;
    uint32_t sink_submit_count;
    uint32_t sink_completed_count;
    uint32_t sink_readback_count;
    uint32_t last_sink_status;
    uint32_t last_pixel_rgba8;
    uint32_t last_checksum;
} AcgcPcMetalRuntimeSnapshot;

/* Register the borrowed GX handoff synchronously before game boot. */
void pc_metal_runtime_init(void);

/* Clear the GX handoff and unregister the borrowed consumer callback. */
void pc_metal_runtime_shutdown(void);

/* Copy the bounded diagnostic state without exposing the runtime context. */
void pc_metal_runtime_get_snapshot(AcgcPcMetalRuntimeSnapshot* snapshot);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_PC_METAL_RUNTIME_H */
