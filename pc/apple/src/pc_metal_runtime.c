#include "acgc/pc_metal_runtime.h"

#include "acgc/apple_canonical_plan_handoff.h"
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

    /* Legacy V2/V3/V4 packets are partial contracts, even when their typed
     * consumer statuses are well-formed. Only V1 has the complete semantics
     * understood by the current geometry-only sink; newer rendering requires
     * an explicit cumulative canonical CPU plan. */
    return output->semantic_version == ACGC_GX_SEMANTIC_PACKET_VERSION &&
        output->source_kind == ACGC_METAL_PACKET_CONSUMER_SOURCE_SEMANTIC;
}

#ifndef ACGC_PC_METAL_RUNTIME_SINK_POLICY_FIXTURE
static int pc_metal_runtime_canonical_sink_eligible(
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
) {
    /* Canonical output is deliberately source-aware. It is not semantic V1:
     * its packet version is zero and every extension status is zero. */
    return status == ACGC_METAL_PACKET_CONSUMER_OK && output != NULL &&
        output->source_kind == ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN &&
        output->semantic_version == 0 &&
        output->v2_extension_rendering_status == 0 &&
        output->v3_extension_rendering_status == 0 &&
        output->v4_extension_rendering_status == 0;
}
#endif

#ifdef ACGC_PC_METAL_RUNTIME_SINK_POLICY_FIXTURE
/* Keep the production policy static; the bounded fixture calls this wrapper. */
int pc_metal_runtime_sink_eligible_fixture(
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
) {
    AcgcMetalPacketConsumerOutput legacy_output;

    if (output == NULL) {
        return 0;
    }
    /* This wrapper serves the pre-source_kind policy fixture only. Its zero
     * initialized output predates the explicit source field; production code
     * always reaches the strict source-aware predicate below. */
    legacy_output = *output;
    if (legacy_output.source_kind == ACGC_METAL_PACKET_CONSUMER_SOURCE_NONE) {
        legacy_output.source_kind = ACGC_METAL_PACKET_CONSUMER_SOURCE_SEMANTIC;
    }
    return pc_metal_runtime_sink_eligible(&legacy_output, status);
}
#else

#ifndef ACGC_PC_METAL_RUNTIME_FAKE_CPU_SINK_FIXTURE
#include "pc_gx_internal.h"
#endif

#include <stdatomic.h>
#include <string.h>

/* The semantic callbacks are value-only and do not require the PC state ABI. */
typedef void (*PcMetalRuntimeSemanticHandoffCallback)(
    void* context,
    const AcgcGxSemanticPacket* packet
);
typedef void (*PcMetalRuntimeSemanticV2HandoffCallback)(
    void* context,
    const AcgcGxSemanticPacketV2* packet
);
typedef void (*PcMetalRuntimeSemanticV3HandoffCallback)(
    void* context,
    const AcgcGxSemanticPacketV3* packet
);
typedef void (*PcMetalRuntimeSemanticV4HandoffCallback)(
    void* context,
    const AcgcGxSemanticPacketV4* packet
);

extern void pc_gx_set_semantic_packet_handoff(
    PcMetalRuntimeSemanticHandoffCallback callback,
    void* context
);
extern void pc_gx_clear_semantic_packet_handoff(void);
extern void pc_gx_set_semantic_packet_v2_handoff(
    PcMetalRuntimeSemanticV2HandoffCallback callback,
    void* context
);
extern void pc_gx_clear_semantic_packet_v2_handoff(void);
extern void pc_gx_set_semantic_packet_v3_handoff(
    PcMetalRuntimeSemanticV3HandoffCallback callback,
    void* context
);
extern void pc_gx_clear_semantic_packet_v3_handoff(void);
extern void pc_gx_set_semantic_packet_v4_handoff(
    PcMetalRuntimeSemanticV4HandoffCallback callback,
    void* context
);
extern void pc_gx_clear_semantic_packet_v4_handoff(void);

extern int acgc_apple_canonical_plan_handoff_set_consumer(
    AcgcAppleCanonicalPlanHandoffConsumer consumer,
    void* context
);
extern int acgc_apple_canonical_plan_handoff_clear_consumer(void);

typedef struct AcgcPcMetalRuntime {
    AcgcMetalPacketConsumerHandoffContext handoff;
    AcgcMetalPacketConsumerOutput output;
    atomic_uint_least32_t registered;
    atomic_uint_least32_t handoff_count;
    atomic_uint_least32_t accepted_count;
    atomic_uint_least32_t rejected_count;
    atomic_uint_least32_t last_status;
    uint32_t canonical_consumer_registered;
    uint64_t current_attempt_id;
    uint64_t last_canonical_attempt_id;
    uint32_t callback_active;
    uint32_t canonical_won;
    uint32_t canonical_attempt_count;
    uint32_t canonical_published_count;
    uint32_t canonical_rejected_count;
    uint32_t canonical_won_count;
    uint32_t canonical_sink_failure_count;
    uint32_t semantic_suppressed_count;
    uint32_t canonical_last_result;
    uint32_t canonical_last_status;
    uint32_t canonical_last_sink_status;
} AcgcPcMetalRuntime;

static AcgcPcMetalRuntime s_pc_metal_runtime = {
    .last_status = ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID,
    .canonical_last_status = ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID,
    .canonical_last_sink_status = ACGC_METAL_SINK_NOT_INITIALIZED
};

#ifndef ACGC_PC_METAL_RUNTIME_FAKE_CPU_SINK_FIXTURE
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
#else
static int pc_metal_runtime_get_v2_texture_source(
    void* context,
    uint32_t map,
    AcgcMetalPacketConsumerV2TextureSource* destination
) {
    (void)context;
    (void)map;
    (void)destination;
    return 0;
}
#endif

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
    s_pc_metal_runtime.canonical_consumer_registered = 0;
    s_pc_metal_runtime.current_attempt_id = 0;
    s_pc_metal_runtime.last_canonical_attempt_id = 0;
    s_pc_metal_runtime.callback_active = 0;
    s_pc_metal_runtime.canonical_won = 0;
    s_pc_metal_runtime.canonical_attempt_count = 0;
    s_pc_metal_runtime.canonical_published_count = 0;
    s_pc_metal_runtime.canonical_rejected_count = 0;
    s_pc_metal_runtime.canonical_won_count = 0;
    s_pc_metal_runtime.canonical_sink_failure_count = 0;
    s_pc_metal_runtime.semantic_suppressed_count = 0;
    s_pc_metal_runtime.canonical_last_result =
        ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION;
    s_pc_metal_runtime.canonical_last_status =
        ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    s_pc_metal_runtime.canonical_last_sink_status =
        ACGC_METAL_SINK_NOT_INITIALIZED;
}

static void pc_metal_runtime_observe_canonical_plan(
    void* context,
    uint64_t attempt_id,
    AcgcAppleCanonicalPlanHandoffResult result,
    const AcgcAppleCanonicalPlan* plan
) {
    AcgcMetalPacketConsumerStatus status =
        ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    AcgcMetalSinkStatus sink_status = ACGC_METAL_SINK_NOT_INITIALIZED;
    AcgcPcMetalRuntime* runtime = (AcgcPcMetalRuntime*)context;

    if (runtime != &s_pc_metal_runtime ||
        atomic_load_explicit(&runtime->registered, memory_order_acquire) == 0) {
        return;
    }
    if (runtime->callback_active != 0) {
        return;
    }
    if (attempt_id == 0 ||
        (runtime->last_canonical_attempt_id != 0 &&
         attempt_id <= runtime->last_canonical_attempt_id)) {
        /* A duplicate/stale/invalid event is a fail-closed invalidation. The
         * handoff has no new borrowed plan to prepare, but a prior winner must
         * not suppress the semantic callback that follows. */
        runtime->canonical_won = 0;
        runtime->canonical_last_result =
            ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION;
        runtime->canonical_last_status =
            ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
        runtime->canonical_last_sink_status = ACGC_METAL_SINK_NOT_INITIALIZED;
        return;
    }

    runtime->callback_active = 1;
    runtime->last_canonical_attempt_id = attempt_id;
    runtime->current_attempt_id = attempt_id;
    runtime->canonical_won = 0;
    runtime->canonical_last_result = (uint32_t)result;
    runtime->canonical_last_status =
        ACGC_METAL_PACKET_CONSUMER_OUTPUT_INVALID;
    runtime->canonical_last_sink_status = ACGC_METAL_SINK_NOT_INITIALIZED;
    pc_metal_runtime_increment(&runtime->handoff_count);
    if (runtime->canonical_attempt_count != UINT32_MAX) {
        runtime->canonical_attempt_count++;
    }

    if (result == ACGC_APPLE_CANONICAL_PLAN_HANDOFF_PLAN_PUBLISHED &&
        plan != NULL) {
        if (runtime->canonical_published_count != UINT32_MAX) {
            runtime->canonical_published_count++;
        }
        status = acgc_metal_packet_consumer_prepare_canonical_plan(
            plan,
            &runtime->output
        );
        runtime->handoff.status = status;
        runtime->canonical_last_status = (uint32_t)status;
        atomic_store_explicit(
            &runtime->last_status,
            (uint_least32_t)status,
            memory_order_release
        );
        if (pc_metal_runtime_canonical_sink_eligible(
                &runtime->output,
                status
            )) {
            sink_status = acgc_metal_sink_submit(&runtime->output);
            runtime->canonical_last_sink_status = (uint32_t)sink_status;
            if (sink_status == ACGC_METAL_SINK_OK) {
                runtime->canonical_won = 1;
                if (runtime->canonical_won_count != UINT32_MAX) {
                    runtime->canonical_won_count++;
                }
            } else if (runtime->canonical_sink_failure_count != UINT32_MAX) {
                runtime->canonical_sink_failure_count++;
            }
        } else if (runtime->canonical_rejected_count != UINT32_MAX) {
            runtime->canonical_rejected_count++;
        }
    } else if (runtime->canonical_rejected_count != UINT32_MAX &&
               (result != ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION ||
                plan != NULL)) {
        /* A rejected classification, or any plan/result mismatch, is fail
         * closed without a sink submission. */
        runtime->canonical_rejected_count++;
    }

    runtime->callback_active = 0;
}

static void pc_metal_runtime_observe(
    void* context,
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
) {
    AcgcPcMetalRuntime* runtime = (AcgcPcMetalRuntime*)context;

    if (runtime != &s_pc_metal_runtime ||
        atomic_load_explicit(&runtime->registered, memory_order_acquire) == 0 ||
        runtime->callback_active != 0) {
        return;
    }

    runtime->callback_active = 1;
    /* The output is borrowed; the sink copies only bounded value fields. */
    pc_metal_runtime_increment(&runtime->handoff_count);
    atomic_store_explicit(
        &runtime->last_status,
        (uint_least32_t)status,
        memory_order_release
    );
    if (status == ACGC_METAL_PACKET_CONSUMER_OK) {
        pc_metal_runtime_increment(&runtime->accepted_count);
        if (runtime->canonical_won && runtime->current_attempt_id != 0) {
            if (runtime->semantic_suppressed_count != UINT32_MAX) {
                runtime->semantic_suppressed_count++;
            }
            runtime->callback_active = 0;
            return;
        }
        if (pc_metal_runtime_sink_eligible(output, status)) {
            (void)acgc_metal_sink_submit(output);
        }
    } else {
        pc_metal_runtime_increment(&runtime->rejected_count);
    }
    runtime->callback_active = 0;
}

int pc_metal_runtime_bind_v2_texture_sideband(
    const AcgcMetalPacketConsumerV2TextureFixture* textures,
    uint32_t texture_count
) {
    if (s_pc_metal_runtime.callback_active != 0) {
        return 0;
    }
    {
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
}

void pc_metal_runtime_clear_v2_texture_sideband(void) {
    if (s_pc_metal_runtime.callback_active != 0) {
        return;
    }
    acgc_metal_packet_consumer_clear_v2_texture_sideband(
        &s_pc_metal_runtime.handoff
    );
}

void pc_metal_runtime_init(void) {
    AcgcMetalPacketConsumerHandoffContext* handoff =
        &s_pc_metal_runtime.handoff;

    if (s_pc_metal_runtime.callback_active != 0 ||
        atomic_load_explicit(
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

    if (acgc_apple_canonical_plan_handoff_set_consumer(
            pc_metal_runtime_observe_canonical_plan,
            &s_pc_metal_runtime
        )) {
        s_pc_metal_runtime.canonical_consumer_registered = 1;
    }

    pc_gx_set_semantic_packet_handoff(
        (PcMetalRuntimeSemanticHandoffCallback)
            acgc_metal_packet_consumer_handoff,
        handoff
    );
    pc_gx_set_semantic_packet_v2_handoff(
        (PcMetalRuntimeSemanticV2HandoffCallback)
            acgc_metal_packet_consumer_handoff_v2,
        handoff
    );
    pc_gx_set_semantic_packet_v3_handoff(
        (PcMetalRuntimeSemanticV3HandoffCallback)
            acgc_metal_packet_consumer_handoff_v3,
        handoff
    );
    pc_gx_set_semantic_packet_v4_handoff(
        (PcMetalRuntimeSemanticV4HandoffCallback)
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
    if (s_pc_metal_runtime.callback_active != 0) {
        return;
    }
    if (atomic_load_explicit(
            &s_pc_metal_runtime.registered,
            memory_order_acquire
        ) == 0) {
        pc_metal_runtime_clear_v2_texture_sideband();
        acgc_metal_sink_shutdown();
        return;
    }

    /* Clear the canonical borrower before stopping the semantic callbacks. */
    if (s_pc_metal_runtime.canonical_consumer_registered != 0 &&
        !acgc_apple_canonical_plan_handoff_clear_consumer()) {
        return;
    }
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
    memset(&s_pc_metal_runtime.output, 0, sizeof(s_pc_metal_runtime.output));
    s_pc_metal_runtime.current_attempt_id = 0;
    s_pc_metal_runtime.last_canonical_attempt_id = 0;
    s_pc_metal_runtime.canonical_won = 0;
    s_pc_metal_runtime.canonical_consumer_registered = 0;
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
    snapshot->current_attempt_id = s_pc_metal_runtime.current_attempt_id;
    snapshot->canonical_attempt_count =
        s_pc_metal_runtime.canonical_attempt_count;
    snapshot->canonical_published_count =
        s_pc_metal_runtime.canonical_published_count;
    snapshot->canonical_rejected_count =
        s_pc_metal_runtime.canonical_rejected_count;
    snapshot->canonical_won_count = s_pc_metal_runtime.canonical_won_count;
    snapshot->canonical_sink_failure_count =
        s_pc_metal_runtime.canonical_sink_failure_count;
    snapshot->semantic_suppressed_count =
        s_pc_metal_runtime.semantic_suppressed_count;
    snapshot->canonical_last_result =
        s_pc_metal_runtime.canonical_last_result;
    snapshot->canonical_last_status =
        s_pc_metal_runtime.canonical_last_status;
    snapshot->canonical_last_sink_status =
        s_pc_metal_runtime.canonical_last_sink_status;
}

#ifdef ACGC_PC_METAL_RUNTIME_FAKE_CPU_SINK_FIXTURE
void pc_metal_runtime_consume_canonical_plan_fixture(
    uint64_t attempt_id,
    AcgcAppleCanonicalPlanHandoffResult result,
    const AcgcAppleCanonicalPlan* plan
) {
    pc_metal_runtime_observe_canonical_plan(
        &s_pc_metal_runtime,
        attempt_id,
        result,
        plan
    );
}

void pc_metal_runtime_observe_output_fixture(
    const AcgcMetalPacketConsumerOutput* output,
    AcgcMetalPacketConsumerStatus status
) {
    pc_metal_runtime_observe(&s_pc_metal_runtime, output, status);
}

int pc_metal_runtime_callback_active_fixture(void) {
    return s_pc_metal_runtime.callback_active != 0;
}
#endif

#endif /* ACGC_PC_METAL_RUNTIME_SINK_POLICY_FIXTURE */
