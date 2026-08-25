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

/* Stable value-only attribution for the first existing INVALID_OUTPUT check. */
typedef enum AcgcMetalSinkValidationReason {
    ACGC_METAL_SINK_VALIDATION_REASON_NONE = 0,
    ACGC_METAL_SINK_VALIDATION_REASON_NULL_OR_SOURCE_KIND = 1,
    ACGC_METAL_SINK_VALIDATION_REASON_STATE_FIXTURE = 2,
    ACGC_METAL_SINK_VALIDATION_REASON_GEOMETRY_FIXTURE = 3,
    ACGC_METAL_SINK_VALIDATION_REASON_CANONICAL_TEV_DISPOSITION = 4,
    ACGC_METAL_SINK_VALIDATION_REASON_TEXTURE_REPLACE_TEV_SHAPE = 5,
    ACGC_METAL_SINK_VALIDATION_REASON_TEXTURE_BINDING_SELECTED_MAP = 6,
    ACGC_METAL_SINK_VALIDATION_REASON_TEXTURE_STAGE_MASKS = 7,
    ACGC_METAL_SINK_VALIDATION_REASON_TLUT_STORAGE_TAIL = 8,
    ACGC_METAL_SINK_VALIDATION_REASON_IMAGE_DESCRIPTION_SIZE_SAMPLER_STORAGE_TAIL = 9,
    ACGC_METAL_SINK_VALIDATION_REASON_TLUT_REFERENCE_LINKAGE = 10,
    ACGC_METAL_SINK_VALIDATION_REASON_TEXCOORD_FINITE_TAIL = 11,
    ACGC_METAL_SINK_VALIDATION_REASON_CANONICAL_BLEND = 12,
    ACGC_METAL_SINK_VALIDATION_REASON_CANONICAL_ALPHA = 13,
    ACGC_METAL_SINK_VALIDATION_REASON_CANONICAL_RASTER = 14,
    ACGC_METAL_SINK_VALIDATION_REASON_CANONICAL_FOG = 15,
    ACGC_METAL_SINK_VALIDATION_REASON_VIEWPORT_READBACK_DIMENSIONS = 16,
    ACGC_METAL_SINK_VALIDATION_REASON_DRAW_SHAPE = 17,
    ACGC_METAL_SINK_VALIDATION_REASON_SELECTED_SAMPLER_RESOLUTION = 18
} AcgcMetalSinkValidationReason;

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
    /* First existing validation predicate for the last sink result. */
    uint32_t last_validation_reason;
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
