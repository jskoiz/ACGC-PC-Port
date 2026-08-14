#include "acgc/pc_metal_runtime.h"

#include "acgc/metal_packet_consumer.h"
#include "acgc/metal_sink.h"

#include <stddef.h>

/*
 * Sink submission is an explicit status proof.  A geometry prefix is not
 * sufficient when a typed semantic extension remains outside the sink.
 */
static int pc_metal_runtime_output_statuses_are_supported(
    const AcgcMetalPacketConsumerOutput* output
) {
    if (output == NULL) {
        return 0;
    }

    switch (output->semantic_version) {
        case ACGC_GX_SEMANTIC_PACKET_VERSION:
            return output->v2_extension_rendering_status ==
                    ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_APPLICABLE &&
                output->v3_extension_rendering_status == 0 &&
                output->v4_extension_rendering_status == 0;

        case ACGC_GX_SEMANTIC_PACKET_V2_VERSION:
            /* Both known V2 outcomes carry semantics this sink cannot draw. */
            return output->v2_extension_rendering_status ==
                    ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_RENDERED ||
                output->v2_extension_rendering_status ==
                    ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_CPU_RESOLVED;

        case ACGC_GX_SEMANTIC_PACKET_V3_VERSION:
        case ACGC_GX_SEMANTIC_PACKET_V4_VERSION:
            return output->v2_extension_rendering_status ==
                    ACGC_METAL_PACKET_CONSUMER_V2_EXTENSION_NOT_APPLICABLE &&
                output->v3_extension_rendering_status ==
                    ACGC_METAL_PACKET_CONSUMER_V3_EXTENSION_NOT_RENDERED &&
                output->v4_extension_rendering_status == 0;

        default:
            return 0;
    }
}

static int pc_metal_runtime_sink_eligible(
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
) {
    if (status != ACGC_METAL_PACKET_CONSUMER_OK ||
        !pc_metal_runtime_output_statuses_are_supported(output)) {
        return 0;
    }

    return output->semantic_version == ACGC_GX_SEMANTIC_PACKET_VERSION ||
        output->semantic_version == ACGC_GX_SEMANTIC_PACKET_V4_VERSION;
}

#ifdef ACGC_PC_METAL_RUNTIME_SINK_POLICY_FIXTURE
/* Keep the production policy static; the bounded fixture calls this wrapper. */
int pc_metal_runtime_sink_eligible_fixture(
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
) {
    return pc_metal_runtime_sink_eligible(output, status);
}
#else

#include "pc_gx_internal.h"

#include <stdatomic.h>
#include <string.h>

/* The v2 PC hook is intentionally not added to the legacy v1 internal ABI. */
extern void pc_gx_set_semantic_packet_v2_handoff(
    AcgcMetalPacketConsumerV2HandoffCallback callback,
    void* context
);
extern void pc_gx_clear_semantic_packet_v2_handoff(void);
extern void pc_gx_set_semantic_packet_v3_handoff(
    AcgcMetalPacketConsumerV3HandoffCallback callback,
    void* context
);
extern void pc_gx_clear_semantic_packet_v3_handoff(void);
extern void pc_gx_set_semantic_packet_v4_handoff(
    AcgcMetalPacketConsumerV4HandoffCallback callback,
    void* context
);
extern void pc_gx_clear_semantic_packet_v4_handoff(void);

/*
 * Registration and teardown are main-thread lifecycle operations. Packet
 * handoff itself is synchronous; atomics keep the bounded observations safe
 * for a diagnostic reader without widening the callback/context contract.
 */
typedef struct AcgcPcMetalRuntime {
    AcgcMetalPacketConsumerHandoffContext handoff;
    AcgcMetalPacketConsumerOutput output;
    atomic_uint_least32_t registered;
    atomic_uint_least32_t handoff_count;
    atomic_uint_least32_t accepted_count;
    atomic_uint_least32_t rejected_count;
    atomic_uint_least32_t last_status;
} AcgcPcMetalRuntime;

static AcgcPcMetalRuntime s_pc_metal_runtime = {
    .last_status = ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID
};

static int pc_metal_runtime_get_v2_texture_source(
    void* context,
    uint32_t map,
    AcgcMetalPacketConsumerV2TextureSource* destination
) {
    PCGXTextureSource source;

    if (context != &s_pc_metal_runtime || destination == NULL || map >= 8 ||
        !pc_gx_get_v2_texture_source((int)map, &source)) {
        return 0;
    }

    /* The Apple record is a value-only mirror; all pointed-to bytes remain
     * borrowed for this synchronous handoff and are generation-checked by the
     * consumer after CPU decode. */
    destination->image_ptr = source.image_ptr;
    destination->image_byte_size = source.image_byte_size;
    destination->tlut_ptr = source.tlut_ptr;
    destination->tlut_byte_size = source.tlut_byte_size;
    destination->tlut_format = source.tlut_format;
    destination->tlut_entries = source.tlut_entries;
    destination->tlut_name = source.tlut_name;
    destination->tlut_is_be = source.tlut_is_be;
    destination->width = source.width;
    destination->height = source.height;
    destination->format = source.format;
    destination->wrap_s = source.wrap_s;
    destination->wrap_t = source.wrap_t;
    destination->min_filter = source.min_filter;
    destination->mag_filter = source.mag_filter;
    destination->effective_filter = source.effective_filter;
    destination->source_kind = source.source_kind;
    destination->tlut_source_kind = source.tlut_source_kind;
    destination->generation = source.generation;
    return 1;
}

static void pc_metal_runtime_increment(atomic_uint_least32_t* counter) {
    uint_least32_t expected =
        atomic_load_explicit(counter, memory_order_relaxed);

    while (expected != UINT32_MAX &&
           !atomic_compare_exchange_weak_explicit(
               counter,
               &expected,
               expected + 1,
               memory_order_relaxed,
               memory_order_relaxed
           )) {
        /* expected is refreshed by a failed compare-exchange. */
    }
}

static void pc_metal_runtime_reset_observations(void) {
    atomic_store_explicit(
        &s_pc_metal_runtime.handoff_count,
        0,
        memory_order_relaxed
    );
    atomic_store_explicit(
        &s_pc_metal_runtime.accepted_count,
        0,
        memory_order_relaxed
    );
    atomic_store_explicit(
        &s_pc_metal_runtime.rejected_count,
        0,
        memory_order_relaxed
    );
    atomic_store_explicit(
        &s_pc_metal_runtime.last_status,
        ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID,
        memory_order_relaxed
    );
}

static void pc_metal_runtime_observe(
    void* context,
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
) {
    AcgcPcMetalRuntime* runtime = (AcgcPcMetalRuntime*)context;

    if (runtime != &s_pc_metal_runtime) {
        return;
    }

    /* The output is borrowed; the sink copies only bounded value fields. */
    pc_metal_runtime_increment(&runtime->handoff_count);
    atomic_store_explicit(
        &runtime->last_status,
        (uint_least32_t)status,
        memory_order_release
    );
    if (status == ACGC_METAL_PACKET_CONSUMER_OK) {
        pc_metal_runtime_increment(&runtime->accepted_count);
        if (pc_metal_runtime_sink_eligible(output, status)) {
            (void)acgc_metal_sink_submit(output);
        }
    } else {
        pc_metal_runtime_increment(&runtime->rejected_count);
    }
}

int pc_metal_runtime_bind_v2_texture_sideband(
    const AcgcMetalPacketConsumerV2TextureFixture* textures,
    uint32_t texture_count
) {
    int result = acgc_metal_packet_consumer_bind_v2_texture_sideband(
        &s_pc_metal_runtime.handoff,
        textures,
        texture_count
    );

    if (result) {
        (void)acgc_metal_packet_consumer_bind_v2_texture_source_provider(
            &s_pc_metal_runtime.handoff,
            pc_metal_runtime_get_v2_texture_source,
            &s_pc_metal_runtime
        );
    }
    return result;
}

void pc_metal_runtime_clear_v2_texture_sideband(void) {
    acgc_metal_packet_consumer_clear_v2_texture_sideband(
        &s_pc_metal_runtime.handoff
    );
}

void pc_metal_runtime_init(void) {
    AcgcMetalPacketConsumerHandoffContext* handoff =
        &s_pc_metal_runtime.handoff;

    if (atomic_load_explicit(
            &s_pc_metal_runtime.registered,
            memory_order_acquire
        ) != 0) {
        return;
    }

    (void)acgc_metal_sink_init();
    memset(&s_pc_metal_runtime.output, 0, sizeof(s_pc_metal_runtime.output));
    handoff->texture = NULL;
    handoff->output = &s_pc_metal_runtime.output;
    handoff->status = ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    handoff->runtime_callback = NULL;
    handoff->runtime_callback_context = NULL;
    acgc_metal_packet_consumer_clear_v2_texture_sideband(handoff);
    pc_metal_runtime_reset_observations();

    if (!acgc_metal_packet_consumer_bind_v2_texture_source_provider(
            handoff,
            pc_metal_runtime_get_v2_texture_source,
            &s_pc_metal_runtime
    ) || !acgc_metal_packet_consumer_register_runtime_callback(
            handoff,
            pc_metal_runtime_observe,
            &s_pc_metal_runtime
    )) {
        pc_metal_runtime_clear_v2_texture_sideband();
        handoff->output = NULL;
        acgc_metal_sink_shutdown();
        return;
    }

    pc_gx_set_semantic_packet_handoff(
        acgc_metal_packet_consumer_handoff,
        handoff
    );
    pc_gx_set_semantic_packet_v2_handoff(
        acgc_metal_packet_consumer_handoff_v2,
        handoff
    );
    pc_gx_set_semantic_packet_v3_handoff(
        acgc_metal_packet_consumer_handoff_v3,
        handoff
    );
    pc_gx_set_semantic_packet_v4_handoff(
        acgc_metal_packet_consumer_handoff_v4,
        handoff
    );
    atomic_store_explicit(
        &s_pc_metal_runtime.registered,
        1,
        memory_order_release
    );
}

void pc_metal_runtime_shutdown(void) {
    if (atomic_load_explicit(
            &s_pc_metal_runtime.registered,
            memory_order_acquire
        ) == 0) {
        pc_metal_runtime_clear_v2_texture_sideband();
        acgc_metal_sink_shutdown();
        return;
    }

    /* Stop new GX calls before clearing the consumer's borrowed callback. */
    pc_gx_clear_semantic_packet_v4_handoff();
    pc_gx_clear_semantic_packet_v3_handoff();
    pc_gx_clear_semantic_packet_v2_handoff();
    pc_gx_clear_semantic_packet_handoff();
    acgc_metal_packet_consumer_unregister_runtime_callback(
        &s_pc_metal_runtime.handoff
    );
    pc_metal_runtime_clear_v2_texture_sideband();
    s_pc_metal_runtime.handoff.texture = NULL;
    s_pc_metal_runtime.handoff.output = NULL;
    atomic_store_explicit(
        &s_pc_metal_runtime.registered,
        0,
        memory_order_release
    );
    acgc_metal_sink_shutdown();
}

void pc_metal_runtime_get_snapshot(AcgcPcMetalRuntimeSnapshot* snapshot) {
    AcgcMetalSinkSnapshot sink_snapshot;

    if (snapshot == NULL) {
        return;
    }

    acgc_metal_sink_get_snapshot(&sink_snapshot);

    snapshot->registered = atomic_load_explicit(
        &s_pc_metal_runtime.registered,
        memory_order_acquire
    );
    snapshot->handoff_count = atomic_load_explicit(
        &s_pc_metal_runtime.handoff_count,
        memory_order_relaxed
    );
    snapshot->accepted_count = atomic_load_explicit(
        &s_pc_metal_runtime.accepted_count,
        memory_order_relaxed
    );
    snapshot->rejected_count = atomic_load_explicit(
        &s_pc_metal_runtime.rejected_count,
        memory_order_relaxed
    );
    snapshot->last_status = atomic_load_explicit(
        &s_pc_metal_runtime.last_status,
        memory_order_acquire
    );
    snapshot->sink_initialized = sink_snapshot.initialized;
    snapshot->sink_available = sink_snapshot.available;
    snapshot->sink_submit_count = sink_snapshot.submit_count;
    snapshot->sink_completed_count = sink_snapshot.completed_count;
    snapshot->sink_readback_count = sink_snapshot.readback_count;
    snapshot->last_sink_status = sink_snapshot.last_status;
    snapshot->last_pixel_rgba8 = sink_snapshot.last_pixel_rgba8;
    snapshot->last_checksum = sink_snapshot.last_checksum;
}

#endif /* ACGC_PC_METAL_RUNTIME_SINK_POLICY_FIXTURE */
