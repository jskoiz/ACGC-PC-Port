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

/* The typed v2 handoff prepares only the embedded v1 geometry prefix. */
#define ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_APPLICABLE UINT32_C(0)
#define ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_RENDERED UINT32_C(1)
#define ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_CPU_RESOLVED UINT32_C(2)
#define ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED UINT32_C(1)
#define ACGC_METAL_PACKET_CONSUMER_V4_EXTENSION_NOT_RENDERED UINT32_C(1)

#define ACGC_METAL_PACKET_CONSUMER_MAX_V2_TEXTURE_FIXTURES \
    ACGC_GX_SEMANTIC_MAX_TEXTURE_GENERATORS

typedef struct AcgcMetalPacketConsumerTexture {
    /* This key must match packet->material.texture0_key. */
    uint32_t key;
    /* A resolved color supplied by the existing texture/TEV fixture seam. */
    AcgcRendererFixtureColor color;
} AcgcMetalPacketConsumerTexture;

/*
 * Caller-owned host storage for one resolved v2 texture generator.  The
 * packet carries only stable keys and dimensions; this record supplies the
 * already-resolved base-level bytes needed by the CPU fixture.  `decoded_rgba`
 * is also caller-owned so this seam never allocates or retains host memory.
 */
typedef struct AcgcMetalPacketConsumerV2TextureFixture {
    uint32_t key;
    AcgcRendererFixtureTextureDescription description;
    const uint8_t* data;
    const uint8_t* tlut_data;
    uint8_t* decoded_rgba;
    uint32_t decoded_rgba_capacity;
    AcgcRendererFixtureSamplerDescription sampler;
} AcgcMetalPacketConsumerV2TextureFixture;

typedef struct AcgcMetalPacketConsumerOutput {
    AcgcMetalStateFixture state;
    AcgcRendererGeometryPacket geometry;
    AcgcRendererFixtureColor texture0_color;
    uint32_t material_flags;
    uint32_t texture0_key;
    uint32_t semantic_version;
    /* Mirrors GXSetAlphaUpdate; color writes remain enabled independently. */
    uint32_t alpha_write_enabled;
    uint32_t v2_extension_rendering_status;
    uint32_t v3_extension_rendering_status;
    uint32_t v4_extension_rendering_status;
    /* CPU-only v2 fixture result for vertex zero; no native texture object. */
    AcgcRendererFixtureColor v2_tev_color;
} AcgcMetalPacketConsumerOutput;

typedef enum AcgcMetalPacketConsumerStatus {
    ACGC_METAL_PACKET_CONSUMER_OK = 0,
    ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT,
    ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET,
    ACGC_METAL_PACKET_CONSUMER_UNSUPPORTED_TOPOLOGY,
    ACGC_METAL_PACKET_CONSUMER_TEXTURE_REQUIRED,
    ACGC_METAL_PACKET_CONSUMER_TEXTURE_KEY_MISMATCH,
    ACGC_METAL_PACKET_CONSUMER_TRANSFORM_OVERFLOW,
    ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID,
    ACGC_METAL_PACKET_CONSUMER_TEXTURE_FIXTURE_INVALID,
    ACGC_METAL_PACKET_CONSUMER_TEV_STATE_UNSUPPORTED
} AcgcMetalPacketConsumerStatus;

/*
 * The one synchronous callback boundary owned by the Apple runtime. `output`
 * is non-NULL only for an OK status and remains borrowed for the duration of
 * the callback. The callback, its context, and the output storage are all
 * caller-owned; this seam never allocates, retains, or disposes them.
 */
typedef void (*AcgcMetalPacketConsumerRuntimeCallback)(
    void* context,
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
);

/* A separate typed callback prevents a v2 prefix from entering the v1 seam. */
typedef void (*AcgcMetalPacketConsumerV2HandoffCallback)(
    void* context,
    const AcgcGxSemanticPacketV2* packet
);

/* V3 forwards the live blend/texture-matrix state without rendering it. */
typedef void (*AcgcMetalPacketConsumerV3HandoffCallback)(
    void* context,
    const AcgcGxSemanticPacketV3* packet
);

/* V4 maps the bounded blend/alpha subset; texture matrices remain separate. */
typedef void (*AcgcMetalPacketConsumerV4HandoffCallback)(
    void* context,
    const AcgcGxSemanticPacketV4* packet
);

typedef struct AcgcMetalPacketConsumerHandoffContext {
    const AcgcMetalPacketConsumerTexture* texture;
    AcgcMetalPacketConsumerOutput* output;
    AcgcMetalPacketConsumerStatus status;
    AcgcMetalPacketConsumerRuntimeCallback runtime_callback;
    void* runtime_callback_context;
} AcgcMetalPacketConsumerHandoffContext;

/* Register or replace the borrowed Apple runtime callback. */
int acgc_metal_packet_consumer_register_runtime_callback(
    AcgcMetalPacketConsumerHandoffContext* handoff,
    AcgcMetalPacketConsumerRuntimeCallback callback,
    void* context
);

/* Clear the callback slot without changing caller-owned packet/output state. */
void acgc_metal_packet_consumer_unregister_runtime_callback(
    AcgcMetalPacketConsumerHandoffContext* handoff
);

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

/* Validate v2, then prepare only its embedded v1 geometry prefix. */
AcgcMetalPacketConsumerStatus acgc_metal_packet_consumer_prepare_v2(
    const AcgcGxSemanticPacketV2* packet,
    const AcgcMetalPacketConsumerTexture* texture,
    AcgcMetalPacketConsumerOutput* output
);

/*
 * Resolve the validated v2 texture/TLUT/TEV extension through the existing
 * renderer-neutral CPU fixtures.  This is an explicit opt-in seam: it uses
 * caller-provided synthetic base-level bytes, evaluates the first decoded
 * base-level texel per vertex (sampler state is validated, not sampled), and
 * never creates a Metal object or submits a draw.
 * `texture_count` must equal packet->texture_generator_count and each key must
 * match exactly one packet generator.  The normal typed v2 handoff above is
 * intentionally unchanged and remains NOT_RENDERED.
 */
AcgcMetalPacketConsumerStatus
acgc_metal_packet_consumer_prepare_v2_texture_tev(
    const AcgcGxSemanticPacketV2* packet,
    const AcgcMetalPacketConsumerV2TextureFixture* textures,
    uint32_t texture_count,
    AcgcMetalPacketConsumerOutput* output
);

/* Validate v3 and prepare only its v1 geometry for bounded observation. */
AcgcMetalPacketConsumerStatus acgc_metal_packet_consumer_prepare_v3(
    const AcgcGxSemanticPacketV3* packet,
    const AcgcMetalPacketConsumerTexture* texture,
    AcgcMetalPacketConsumerOutput* output
);

/* Validate v4 and prepare its bounded geometry/blend/alpha subset. */
AcgcMetalPacketConsumerStatus acgc_metal_packet_consumer_prepare_v4(
    const AcgcGxSemanticPacketV4* packet,
    const AcgcMetalPacketConsumerTexture* texture,
    AcgcMetalPacketConsumerOutput* output
);

/* Prepare one packet for the existing Apple fixture consumer. */
void acgc_metal_packet_consumer_handoff(
    void* context,
    const AcgcGxSemanticPacket* packet
);

/* Prepare one validated v2 packet without invoking the v1 callback type. */
void acgc_metal_packet_consumer_handoff_v2(
    void* context,
    const AcgcGxSemanticPacketV2* packet
);

/* Prepare one validated v3 state-forwarding packet without rendering it. */
void acgc_metal_packet_consumer_handoff_v3(
    void* context,
    const AcgcGxSemanticPacketV3* packet
);

/* Prepare one validated v4 packet for the bounded Apple runtime sink. */
void acgc_metal_packet_consumer_handoff_v4(
    void* context,
    const AcgcGxSemanticPacketV4* packet
);

const char* acgc_metal_packet_consumer_status_string(
    AcgcMetalPacketConsumerStatus status
);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_METAL_PACKET_CONSUMER_H */
