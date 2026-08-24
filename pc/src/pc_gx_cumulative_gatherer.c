#include "pc_gx_cumulative_gatherer.h"

#include "pc_gx_blend_producer.h"
#include "pc_gx_depth_producer.h"
#include "pc_gx_fog_producer.h"
#include "pc_gx_geometry_dependencies.h"
#include "pc_gx_geometry_producer.h"
#include "pc_gx_indirect_producer.h"
#include "pc_gx_tev_producer.h"
#include "pc_gx_texgen_producer.h"
#include "pc_gx_transform_producer.h"
#include "pc_gx_texture_raw_state.h"

#include "acgc/gx_canonical_alpha_state.h"
#include "acgc/gx_canonical_blend_state.h"
#include "acgc/gx_canonical_channel_state.h"
#include "acgc/gx_canonical_dynamic_state.h"
#include "acgc/gx_canonical_geometry_state.h"
#include "acgc/gx_canonical_indirect_state.h"
#include "acgc/gx_canonical_lighting_state.h"
#include "acgc/gx_canonical_raster_state.h"
#include "acgc/gx_canonical_tev_state.h"
#include "acgc/gx_canonical_texgen_state.h"
#include "acgc/gx_canonical_texture_state.h"
#include "acgc/gx_canonical_transform_state.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PC_GX_GATHERER_GEOMETRY_OFFSET UINT32_C(0)
#define PC_GX_GATHERER_TRANSFORM_OFFSET \
    (PC_GX_GATHERER_GEOMETRY_OFFSET + \
     ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE)
#define PC_GX_GATHERER_CHANNEL_OFFSET \
    (PC_GX_GATHERER_TRANSFORM_OFFSET + \
     ACGC_GX_CANONICAL_TRANSFORM_SECTION_BYTE_SIZE)
#define PC_GX_GATHERER_TEXGEN_OFFSET \
    (PC_GX_GATHERER_CHANNEL_OFFSET + \
     ACGC_GX_CANONICAL_CHANNEL_SECTION_BYTE_SIZE)
#define PC_GX_GATHERER_TEXTURE_OFFSET \
    (PC_GX_GATHERER_TEXGEN_OFFSET + \
     ACGC_GX_CANONICAL_TEXGEN_SECTION_BYTE_SIZE)
#define PC_GX_GATHERER_TEV_OFFSET \
    (PC_GX_GATHERER_TEXTURE_OFFSET + \
     ACGC_GX_CANONICAL_TEXTURE_SECTION_BYTE_SIZE)
#define PC_GX_GATHERER_LIGHTING_OFFSET \
    (PC_GX_GATHERER_TEV_OFFSET + \
     ACGC_GX_CANONICAL_TEV_SECTION_BYTE_SIZE)
#define PC_GX_GATHERER_BLEND_OFFSET \
    (PC_GX_GATHERER_LIGHTING_OFFSET + \
     ACGC_GX_CANONICAL_LIGHTING_SECTION_BYTE_SIZE)
#define PC_GX_GATHERER_ALPHA_OFFSET \
    (PC_GX_GATHERER_BLEND_OFFSET + \
     ACGC_GX_CANONICAL_BLEND_SECTION_BYTE_SIZE)
#define PC_GX_GATHERER_DEPTH_OFFSET \
    (PC_GX_GATHERER_ALPHA_OFFSET + \
     ACGC_GX_CANONICAL_ALPHA_SECTION_BYTE_SIZE)
#define PC_GX_GATHERER_RASTER_OFFSET \
    (PC_GX_GATHERER_DEPTH_OFFSET + \
     ACGC_GX_CANONICAL_DEPTH_SECTION_BYTE_SIZE)
#define PC_GX_GATHERER_FOG_OFFSET \
    (PC_GX_GATHERER_RASTER_OFFSET + \
     ACGC_GX_CANONICAL_RASTER_SECTION_BYTE_SIZE)
#define PC_GX_GATHERER_INDIRECT_OFFSET \
    (PC_GX_GATHERER_FOG_OFFSET + ACGC_GX_CANONICAL_FOG_STATE_SIZE)
#define PC_GX_GATHERER_DYNAMIC_OFFSET \
    (PC_GX_GATHERER_INDIRECT_OFFSET + \
     ACGC_GX_CANONICAL_INDIRECT_SECTION_BYTE_SIZE)
#define PC_GX_GATHERER_PAYLOAD_END \
    (PC_GX_GATHERER_DYNAMIC_OFFSET + \
     ACGC_GX_CANONICAL_DYNAMIC_SECTION_BYTE_SIZE)

_Static_assert(
    PC_GX_GATHERER_PAYLOAD_END ==
        PC_GX_CUMULATIVE_GATHERER_ENCODED_PAYLOAD_SIZE,
    "cumulative gatherer payload workspace must cover all sections"
);
_Static_assert(
    PC_GX_GATHERER_GEOMETRY_OFFSET +
            ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE <=
        PC_GX_GATHERER_TRANSFORM_OFFSET,
    "Geometry output must not overlap Transform bytes"
);

static PCGXCumulativeSnapshotCallback s_callback;
static PCGXCumulativeSnapshotAttemptCallback s_attempt_callback;
static PCGXCumulativeSnapshotResourceCallback s_resource_callback;
static void* s_callback_context;
static void* s_resource_callback_context;
static uint64_t s_active_attempt_id;
static unsigned int s_callback_dispatch_depth;

static int callback_registration_is_blocked(void) {
    return pc_gx_texture_raw_borrow_is_active() ||
        s_callback_dispatch_depth != 0;
}

static void clear_registered_callbacks(void) {
    s_callback = NULL;
    s_attempt_callback = NULL;
    s_resource_callback = NULL;
    s_callback_context = NULL;
    s_resource_callback_context = NULL;
    s_active_attempt_id = 0;
}

static void set_section(
    PCGXCumulativeSnapshotSection* section,
    uint32_t section_id,
    uint32_t section_version,
    uint32_t count,
    uint32_t capacity,
    uint32_t valid_mask,
    uint8_t* bytes,
    size_t byte_size
) {
    section->section_id = section_id;
    section->section_version = section_version;
    section->byte_size = byte_size;
    section->count = count;
    section->capacity = capacity;
    section->valid_mask = valid_mask;
    section->bytes.data = bytes;
    section->bytes.size = byte_size;
}

int pc_gx_set_cumulative_snapshot_callback(
    PCGXCumulativeSnapshotCallback callback,
    void* context
) {
    if (callback == NULL || s_attempt_callback != NULL ||
        callback_registration_is_blocked()) {
        return 0;
    }
    s_callback = callback;
    s_callback_context = context;
    return 1;
}

int pc_gx_clear_cumulative_snapshot_callback(void) {
    if (s_attempt_callback != NULL || callback_registration_is_blocked()) {
        return 0;
    }
    clear_registered_callbacks();
    return 1;
}

int pc_gx_set_cumulative_snapshot_callbacks(
    PCGXCumulativeSnapshotCallback callback,
    PCGXCumulativeSnapshotAttemptCallback attempt_callback,
    void* context
) {
    if (callback == NULL || attempt_callback == NULL ||
        s_callback != NULL || s_attempt_callback != NULL ||
        callback_registration_is_blocked()) {
        return 0;
    }
    s_callback = callback;
    s_attempt_callback = attempt_callback;
    s_callback_context = context;
    return 1;
}

int pc_gx_clear_cumulative_snapshot_callbacks(void) {
    if (callback_registration_is_blocked()) {
        return 0;
    }
    clear_registered_callbacks();
    return 1;
}

int pc_gx_set_cumulative_snapshot_resource_callback(
    PCGXCumulativeSnapshotResourceCallback callback,
    void* context
) {
    if (callback == NULL || s_resource_callback != NULL ||
        callback_registration_is_blocked()) {
        return 0;
    }
    s_resource_callback = callback;
    s_resource_callback_context = context;
    return 1;
}

int pc_gx_clear_cumulative_snapshot_resource_callback(void) {
    if (callback_registration_is_blocked()) {
        return 0;
    }
    s_resource_callback = NULL;
    s_resource_callback_context = NULL;
    return 1;
}

int pc_gx_set_cumulative_snapshot_attempt_id(uint64_t attempt_id) {
    if (callback_registration_is_blocked()) {
        return 0;
    }
    s_active_attempt_id = attempt_id;
    return 1;
}

int pc_gx_cumulative_snapshot_callback_dispatch_is_active(void) {
    return s_callback_dispatch_depth != 0;
}

static void callback_dispatch_begin(void) {
    if (s_callback_dispatch_depth != UINT_MAX) {
        s_callback_dispatch_depth++;
    }
}

static void callback_dispatch_end(void) {
    if (s_callback_dispatch_depth != 0) {
        s_callback_dispatch_depth--;
    }
}

int pc_gx_notify_cumulative_snapshot_attempt(
    uint64_t attempt_id,
    PCGXCumulativeSnapshotAttemptResult result
) {
    PCGXCumulativeSnapshotAttemptCallback callback;
    void* callback_context;

    if (s_attempt_callback == NULL ||
        pc_gx_cumulative_snapshot_callback_dispatch_is_active()) {
        return 0;
    }
    callback = s_attempt_callback;
    callback_context = s_callback_context;
    callback_dispatch_begin();
    callback(callback_context, attempt_id, result);
    callback_dispatch_end();
    return 1;
}

int pc_gx_cumulative_snapshot_gather(
    const PCGXRawGeometryBatch* completed_geometry,
    PCGXCumulativeSnapshotStorage* storage
) {
    PCGXCumulativeSnapshotCallback callback;
    void* callback_context;
    PCGXCumulativeSnapshotResourceCallback resource_callback;
    void* resource_callback_context;
    uint64_t attempt_id;
    PCGXTextureRawBorrow borrow = {0};
    PCGXTextureRawState texture_raw;
    PCGXTextureDynamicLease texture_lease;
    AcgcGxCanonicalTransformState transform;
    AcgcGxCanonicalChannelState channels;
    AcgcGxCanonicalTexgenState texgen;
    AcgcGxCanonicalTextureState texture;
    AcgcGxCanonicalTevState tev;
    AcgcGxCanonicalLightingState lighting;
    AcgcGxCanonicalBlendState blend;
    AcgcGxCanonicalAlphaState alpha;
    AcgcGxCanonicalDepthState depth;
    AcgcGxCanonicalRasterState raster;
    AcgcGxCanonicalFogState fog;
    AcgcGxCanonicalGeometryDependencyResults geometry_dependencies;
    AcgcGxCanonicalIndirectState indirect;
    AcgcGxCanonicalDynamicState dynamic;
    PCGXCumulativeSnapshotSection sections[
        PC_GX_CUMULATIVE_SNAPSHOT_SECTION_COUNT];
    size_t geometry_byte_size;
    int borrow_active = 0;
    int resource_callback_result = 1;

    if (completed_geometry == NULL || storage == NULL || s_callback == NULL ||
        pc_gx_cumulative_snapshot_callback_dispatch_is_active()) {
        return 0;
    }
    callback = s_callback;
    callback_context = s_callback_context;
    resource_callback = s_resource_callback;
    resource_callback_context = s_resource_callback_context;
    attempt_id = s_active_attempt_id;

    if (!pc_gx_texture_raw_begin_borrow(&borrow)) {
        return 0;
    }
    borrow_active = 1;

    /* All value producers stage their outputs and leave them unchanged on
     * failure.  Texture/Dynamic are one transaction and must be built while
     * this exact borrow remains active. */
    if (!pc_gx_raw_transform_build_canonical(
            &g_gx.raw_transform, &transform) ||
        !pc_gx_raw_channels_build_canonical(&channels) ||
        !pc_gx_raw_texgen_build_canonical(
            &g_gx.raw_texgen, &texgen) ||
        !pc_gx_build_texture_dynamic_snapshot_borrowed(
            &borrow, &texture, &dynamic, &texture_raw, &texture_lease) ||
        !pc_gx_raw_tev_build_canonical(
            &g_gx.raw_tev_indirect, &tev) ||
        !pc_gx_raw_lighting_build_canonical(&lighting) ||
        !pc_gx_raw_blend_build_canonical(
            &g_gx.raw_blend, &blend) ||
        !pc_gx_raw_alpha_build_canonical(&alpha) ||
        !pc_gx_raw_depth_build_canonical(
            &g_gx.raw_depth, &depth) ||
        !pc_gx_raw_raster_build_canonical(&raster) ||
        !pc_gx_raw_fog_build_canonical(
            &g_gx.raw_fog, &fog) ||
        !pc_gx_geometry_build_dependency_results(
            completed_geometry,
            &transform,
            &texgen,
            &channels,
            &lighting,
            &geometry_dependencies) ||
        !pc_gx_geometry_build_canonical(
            completed_geometry,
            &geometry_dependencies,
            storage->encoded_payload + PC_GX_GATHERER_GEOMETRY_OFFSET,
            ACGC_GX_CANONICAL_GEOMETRY_MAX_SECTION_SIZE,
            &geometry_byte_size,
            storage->geometry_scratch,
            PC_GX_CUMULATIVE_GATHERER_GEOMETRY_SCRATCH_SIZE) ||
        !pc_gx_raw_indirect_build_canonical(
            &g_gx.raw_tev_indirect, &indirect)) {
        goto failure;
    }

    /* Every non-Geometry span is produced by an explicit canonical encoder;
     * native struct representation is never handed to the assembler. */
    if (!acgc_gx_canonical_transform_state_encode(
            &transform,
            storage->encoded_payload + PC_GX_GATHERER_TRANSFORM_OFFSET,
            ACGC_GX_CANONICAL_TRANSFORM_SECTION_BYTE_SIZE) ||
        !acgc_gx_canonical_channel_state_encode(
            &channels,
            storage->encoded_payload + PC_GX_GATHERER_CHANNEL_OFFSET,
            ACGC_GX_CANONICAL_CHANNEL_SECTION_BYTE_SIZE) ||
        !acgc_gx_canonical_texgen_state_encode(
            &texgen,
            storage->encoded_payload + PC_GX_GATHERER_TEXGEN_OFFSET,
            ACGC_GX_CANONICAL_TEXGEN_SECTION_BYTE_SIZE) ||
        !acgc_gx_canonical_texture_state_encode(
            &texture,
            storage->encoded_payload + PC_GX_GATHERER_TEXTURE_OFFSET,
            ACGC_GX_CANONICAL_TEXTURE_SECTION_BYTE_SIZE) ||
        !acgc_gx_canonical_tev_state_encode(
            &tev,
            storage->encoded_payload + PC_GX_GATHERER_TEV_OFFSET,
            ACGC_GX_CANONICAL_TEV_SECTION_BYTE_SIZE) ||
        !acgc_gx_canonical_lighting_state_encode(
            &lighting,
            storage->encoded_payload + PC_GX_GATHERER_LIGHTING_OFFSET,
            ACGC_GX_CANONICAL_LIGHTING_SECTION_BYTE_SIZE) ||
        !acgc_gx_canonical_blend_state_encode(
            &blend,
            storage->encoded_payload + PC_GX_GATHERER_BLEND_OFFSET,
            ACGC_GX_CANONICAL_BLEND_SECTION_BYTE_SIZE) ||
        !acgc_gx_canonical_alpha_state_encode(
            &alpha,
            storage->encoded_payload + PC_GX_GATHERER_ALPHA_OFFSET,
            ACGC_GX_CANONICAL_ALPHA_SECTION_BYTE_SIZE) ||
        !acgc_gx_canonical_depth_state_encode(
            &depth,
            storage->encoded_payload + PC_GX_GATHERER_DEPTH_OFFSET,
            ACGC_GX_CANONICAL_DEPTH_SECTION_BYTE_SIZE) ||
        !acgc_gx_canonical_raster_state_encode(
            &raster,
            storage->encoded_payload + PC_GX_GATHERER_RASTER_OFFSET,
            ACGC_GX_CANONICAL_RASTER_SECTION_BYTE_SIZE) ||
        !acgc_gx_canonical_fog_state_encode(
            &fog,
            storage->encoded_payload + PC_GX_GATHERER_FOG_OFFSET,
            ACGC_GX_CANONICAL_FOG_STATE_SIZE) ||
        !acgc_gx_canonical_indirect_state_encode(
            &indirect,
            storage->encoded_payload + PC_GX_GATHERER_INDIRECT_OFFSET,
            ACGC_GX_CANONICAL_INDIRECT_SECTION_BYTE_SIZE) ||
        !acgc_gx_canonical_dynamic_state_encode(
            &dynamic,
            storage->encoded_payload + PC_GX_GATHERER_DYNAMIC_OFFSET,
            ACGC_GX_CANONICAL_DYNAMIC_SECTION_BYTE_SIZE)) {
        goto failure;
    }

    set_section(
        &sections[0],
        ACGC_GX_CANONICAL_GEOMETRY_SECTION_ID,
        ACGC_GX_CANONICAL_GEOMETRY_STATE_VERSION,
        ACGC_GX_CANONICAL_GEOMETRY_STATE_COUNT,
        ACGC_GX_CANONICAL_GEOMETRY_STATE_CAPACITY,
        ACGC_GX_CANONICAL_GEOMETRY_SECTION_MASK,
        storage->encoded_payload + PC_GX_GATHERER_GEOMETRY_OFFSET,
        geometry_byte_size
    );
    set_section(
        &sections[1],
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_ID,
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_VERSION,
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_COUNT,
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_MASK,
        storage->encoded_payload + PC_GX_GATHERER_TRANSFORM_OFFSET,
        ACGC_GX_CANONICAL_TRANSFORM_SECTION_BYTE_SIZE
    );
    set_section(
        &sections[2],
        ACGC_GX_CANONICAL_CHANNEL_SECTION_ID,
        ACGC_GX_CANONICAL_CHANNEL_SECTION_VERSION,
        ACGC_GX_CANONICAL_CHANNEL_SECTION_COUNT,
        ACGC_GX_CANONICAL_CHANNEL_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_CHANNEL_SECTION_MASK,
        storage->encoded_payload + PC_GX_GATHERER_CHANNEL_OFFSET,
        ACGC_GX_CANONICAL_CHANNEL_SECTION_BYTE_SIZE
    );
    set_section(
        &sections[3],
        ACGC_GX_CANONICAL_TEXGEN_SECTION_ID,
        ACGC_GX_CANONICAL_TEXGEN_SECTION_VERSION,
        ACGC_GX_CANONICAL_TEXGEN_SECTION_COUNT,
        ACGC_GX_CANONICAL_TEXGEN_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_TEXGEN_SECTION_MASK,
        storage->encoded_payload + PC_GX_GATHERER_TEXGEN_OFFSET,
        ACGC_GX_CANONICAL_TEXGEN_SECTION_BYTE_SIZE
    );
    set_section(
        &sections[4],
        ACGC_GX_CANONICAL_TEXTURE_SECTION_ID,
        ACGC_GX_CANONICAL_TEXTURE_SECTION_VERSION,
        ACGC_GX_CANONICAL_TEXTURE_SECTION_COUNT,
        ACGC_GX_CANONICAL_TEXTURE_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_TEXTURE_SECTION_MASK,
        storage->encoded_payload + PC_GX_GATHERER_TEXTURE_OFFSET,
        ACGC_GX_CANONICAL_TEXTURE_SECTION_BYTE_SIZE
    );
    set_section(
        &sections[5],
        ACGC_GX_CANONICAL_TEV_SECTION_ID,
        ACGC_GX_CANONICAL_TEV_SECTION_VERSION,
        tev.header.active_stage_count,
        ACGC_GX_CANONICAL_TEV_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_TEV_SECTION_MASK,
        storage->encoded_payload + PC_GX_GATHERER_TEV_OFFSET,
        ACGC_GX_CANONICAL_TEV_SECTION_BYTE_SIZE
    );
    set_section(
        &sections[6],
        ACGC_GX_CANONICAL_LIGHTING_SECTION_ID,
        ACGC_GX_CANONICAL_LIGHTING_SECTION_VERSION,
        ACGC_GX_CANONICAL_LIGHTING_SECTION_COUNT,
        ACGC_GX_CANONICAL_LIGHTING_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_LIGHTING_SECTION_MASK,
        storage->encoded_payload + PC_GX_GATHERER_LIGHTING_OFFSET,
        ACGC_GX_CANONICAL_LIGHTING_SECTION_BYTE_SIZE
    );
    set_section(
        &sections[7],
        ACGC_GX_CANONICAL_BLEND_SECTION_ID,
        ACGC_GX_CANONICAL_BLEND_SECTION_VERSION,
        ACGC_GX_CANONICAL_BLEND_SECTION_COUNT,
        ACGC_GX_CANONICAL_BLEND_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_BLEND_SECTION_MASK,
        storage->encoded_payload + PC_GX_GATHERER_BLEND_OFFSET,
        ACGC_GX_CANONICAL_BLEND_SECTION_BYTE_SIZE
    );
    set_section(
        &sections[8],
        ACGC_GX_CANONICAL_ALPHA_SECTION_ID,
        ACGC_GX_CANONICAL_ALPHA_SECTION_VERSION,
        ACGC_GX_CANONICAL_ALPHA_SECTION_COUNT,
        ACGC_GX_CANONICAL_ALPHA_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_ALPHA_SECTION_MASK,
        storage->encoded_payload + PC_GX_GATHERER_ALPHA_OFFSET,
        ACGC_GX_CANONICAL_ALPHA_SECTION_BYTE_SIZE
    );
    set_section(
        &sections[9],
        ACGC_GX_CANONICAL_DEPTH_SECTION_ID,
        ACGC_GX_CANONICAL_DEPTH_SECTION_VERSION,
        ACGC_GX_CANONICAL_DEPTH_SECTION_COUNT,
        ACGC_GX_CANONICAL_DEPTH_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_DEPTH_SECTION_MASK,
        storage->encoded_payload + PC_GX_GATHERER_DEPTH_OFFSET,
        ACGC_GX_CANONICAL_DEPTH_SECTION_BYTE_SIZE
    );
    set_section(
        &sections[10],
        ACGC_GX_CANONICAL_RASTER_SECTION_ID,
        ACGC_GX_CANONICAL_RASTER_SECTION_VERSION,
        ACGC_GX_CANONICAL_RASTER_SECTION_COUNT,
        ACGC_GX_CANONICAL_RASTER_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_RASTER_SECTION_MASK,
        storage->encoded_payload + PC_GX_GATHERER_RASTER_OFFSET,
        ACGC_GX_CANONICAL_RASTER_SECTION_BYTE_SIZE
    );
    set_section(
        &sections[11],
        ACGC_GX_CANONICAL_SECTION_ID_FOG,
        ACGC_GX_CANONICAL_SECTION_VERSION,
        1,
        1,
        ACGC_GX_CANONICAL_SECTION_MASK_FOG,
        storage->encoded_payload + PC_GX_GATHERER_FOG_OFFSET,
        ACGC_GX_CANONICAL_FOG_STATE_SIZE
    );
    set_section(
        &sections[12],
        ACGC_GX_CANONICAL_INDIRECT_SECTION_ID,
        ACGC_GX_CANONICAL_INDIRECT_SECTION_VERSION,
        ACGC_GX_CANONICAL_INDIRECT_SECTION_COUNT,
        ACGC_GX_CANONICAL_INDIRECT_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_INDIRECT_SECTION_MASK,
        storage->encoded_payload + PC_GX_GATHERER_INDIRECT_OFFSET,
        ACGC_GX_CANONICAL_INDIRECT_SECTION_BYTE_SIZE
    );
    set_section(
        &sections[13],
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_ID,
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_VERSION,
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_COUNT,
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_CAPACITY,
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_MASK,
        storage->encoded_payload + PC_GX_GATHERER_DYNAMIC_OFFSET,
        ACGC_GX_CANONICAL_DYNAMIC_SECTION_BYTE_SIZE
    );

    /* Revalidate immediately before assembly/publication and keep the
     * metadata local until the assembler succeeds. */
    if (!pc_gx_texture_raw_revalidate_borrow(
            &borrow, &texture_raw, &texture_lease) ||
        !pc_gx_cumulative_snapshot_assemble(
            sections,
            storage->envelope,
            PC_GX_CUMULATIVE_SNAPSHOT_MAX_BYTES,
            &storage->envelope_byte_size)) {
        goto failure;
    }

    memcpy(storage->sections, sections, sizeof(sections));

    callback_dispatch_begin();
    callback(
        callback_context,
        storage->envelope,
        storage->envelope_byte_size
    );
    callback_dispatch_end();

    if (resource_callback != NULL) {
        callback_dispatch_begin();
        resource_callback_result = resource_callback(
            resource_callback_context,
            attempt_id,
            storage->envelope,
            storage->envelope_byte_size,
            &texture,
            &dynamic,
            &texture_lease
        );
        callback_dispatch_end();
    }

    /* The resource callback may have synchronously consumed borrowed bytes.
     * Revalidate the exact raw capture and lease after it returns, before
     * ending the borrow or notifying the post-borrow attempt owner. */
    if (resource_callback_result == 0 ||
        !pc_gx_texture_raw_revalidate_borrow(
            &borrow, &texture_raw, &texture_lease)) {
        goto failure;
    }

    borrow_active = 0;
    return pc_gx_texture_raw_end_borrow(&borrow) != 0;

failure:
    if (borrow_active) {
        borrow_active = 0;
        (void)pc_gx_texture_raw_end_borrow(&borrow);
    }
    return 0;
}
