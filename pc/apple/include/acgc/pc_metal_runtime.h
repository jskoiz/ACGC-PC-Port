#ifndef ACGC_PC_METAL_RUNTIME_H
#define ACGC_PC_METAL_RUNTIME_H

#include "acgc/metal_packet_consumer.h"

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

/*
 * Bind a caller-owned V2 texture/TLUT/sampler array for synchronous packet
 * handoff.  The runtime stores only the borrowed pointer/count and never
 * allocates, copies, or retains a native texture object.  Returns zero for an
 * invalid binding and clears any previous source in that case.
 */
int pc_metal_runtime_bind_v2_texture_sideband(
    const AcgcMetalPacketConsumerV2TextureFixture* textures,
    uint32_t texture_count
);

/* Clear the borrowed V2 source before its storage goes out of scope. */
void pc_metal_runtime_clear_v2_texture_sideband(void);

/* Clear the GX handoff and unregister the borrowed consumer callback. */
void pc_metal_runtime_shutdown(void);

/* Copy the bounded diagnostic state without exposing the runtime context. */
void pc_metal_runtime_get_snapshot(AcgcPcMetalRuntimeSnapshot* snapshot);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_PC_METAL_RUNTIME_H */
