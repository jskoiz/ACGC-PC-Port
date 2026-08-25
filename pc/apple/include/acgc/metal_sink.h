#ifndef ACGC_METAL_SINK_H
#define ACGC_METAL_SINK_H

#include "acgc/metal_packet_consumer.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Submit-derived targets use the canonical strict edge bound. */
#define ACGC_METAL_SINK_MAX_EDGE \
    ACGC_GX_CANONICAL_RASTER_SCISSOR_LIMIT
#define ACGC_METAL_SINK_RGBA8_BYTES_PER_PIXEL UINT32_C(4)

typedef enum AcgcMetalSinkStatus {
    ACGC_METAL_SINK_OK = 0,
    ACGC_METAL_SINK_NOT_INITIALIZED,
    ACGC_METAL_SINK_NO_DEVICE,
    ACGC_METAL_SINK_INVALID_OUTPUT,
    ACGC_METAL_SINK_RESOURCE_FAILURE,
    ACGC_METAL_SINK_COMMAND_BUFFER_FAILURE,
    ACGC_METAL_SINK_READBACK_FAILURE
} AcgcMetalSinkStatus;

/*
 * These are value-only observations. The implementation keeps the counters
 * atomic so a diagnostic reader cannot race a synchronous sink submission;
 * no Metal object, command buffer, or guest pointer crosses this boundary.
 */
typedef struct AcgcMetalSinkSnapshot {
    uint32_t initialized;
    uint32_t available;
    uint32_t submit_count;
    uint32_t completed_count;
    uint32_t readback_count;
    uint32_t last_status;
    /* Center pixel from the last successful bounded RGBA8 readback. */
    uint32_t last_pixel_rgba8;
    /* FNV-1a over all bytes from the last successful bounded readback. */
    uint32_t last_checksum;
} AcgcMetalSinkSnapshot;

/* Create or retain the device, command queue, and shader library. */
AcgcMetalSinkStatus acgc_metal_sink_init(void);

/* Release Metal resources after the borrowed GX callback has been cleared. */
void acgc_metal_sink_shutdown(void);

/* Submit one prepared value packet synchronously; the input remains borrowed. */
AcgcMetalSinkStatus acgc_metal_sink_submit(
    const AcgcMetalPacketConsumerOutput* output
);

/* Copy bounded atomic status/readback observations for a C caller. */
void acgc_metal_sink_get_snapshot(AcgcMetalSinkSnapshot* snapshot);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_METAL_SINK_H */
