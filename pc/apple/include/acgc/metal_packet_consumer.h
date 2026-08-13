#ifndef ACGC_METAL_PACKET_CONSUMER_H
#define ACGC_METAL_PACKET_CONSUMER_H

#include "acgc/gx_semantic_packet.h"
#include "acgc/metal_state_fixture.h"
#include "acgc/renderer_fixtures.h"
#include "acgc/renderer_geometry.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A bounded Apple consumer for the existing renderer-neutral GX packet. The
 * current renderer geometry fixture is one non-indexed triangle, so this
 * consumer deliberately rejects larger runs and quads instead of silently
 * truncating them. Normal and texture-coordinate words remain in the input
 * contract and are validated, but lighting and native texture binding stay
 * outside this lane.
 */
#define ACGC_METAL_PACKET_CONSUMER_VERSION UINT32_C(1)

typedef struct AcgcMetalPacketConsumerTexture {
    /* This key must match packet->material.texture0_key. */
    uint32_t key;
    /* A resolved color supplied by the existing texture/TEV fixture seam. */
    AcgcRendererFixtureColor color;
} AcgcMetalPacketConsumerTexture;

typedef struct AcgcMetalPacketConsumerOutput {
    AcgcMetalStateFixture state;
    AcgcRendererGeometryPacket geometry;
    AcgcRendererFixtureColor texture0_color;
    uint32_t material_flags;
    uint32_t texture0_key;
} AcgcMetalPacketConsumerOutput;

typedef enum AcgcMetalPacketConsumerStatus {
    ACGC_METAL_PACKET_CONSUMER_OK = 0,
    ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT,
    ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET,
    ACGC_METAL_PACKET_CONSUMER_UNSUPPORTED_TOPOLOGY,
    ACGC_METAL_PACKET_CONSUMER_TEXTURE_REQUIRED,
    ACGC_METAL_PACKET_CONSUMER_TEXTURE_KEY_MISMATCH,
    ACGC_METAL_PACKET_CONSUMER_TRANSFORM_OVERFLOW,
    ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID
} AcgcMetalPacketConsumerStatus;

/*
 * Convert one validated triangle packet into the existing Apple state and
 * geometry fixture records. No Metal object or native pointer crosses this
 * API. texture may be NULL only when the packet does not request texture0.
 */
AcgcMetalPacketConsumerStatus acgc_metal_packet_consumer_prepare(
    const AcgcGxSemanticPacket* packet,
    const AcgcMetalPacketConsumerTexture* texture,
    AcgcMetalPacketConsumerOutput* output
);

const char* acgc_metal_packet_consumer_status_string(
    AcgcMetalPacketConsumerStatus status
);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_METAL_PACKET_CONSUMER_H */
