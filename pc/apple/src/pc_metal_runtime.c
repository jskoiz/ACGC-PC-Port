#include "acgc/pc_metal_runtime.h"

#include "acgc/metal_packet_consumer.h"
#include "acgc/metal_sink.h"
#include "pc_gx_internal.h"

#include <stdatomic.h>
#include <string.h>

/* The v2 PC hook is intentionally not added to the legacy v1 internal ABI. */
extern void pc_gx_set_semantic_packet_v2_handoff(
    AcgcMetalPacketConsumerV2HandoffCallback callback,
    void* context
);
extern void pc_gx_clear_semantic_packet_v2_handoff(void);

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
        (void)acgc_metal_sink_submit(output);
    } else {
        pc_metal_runtime_increment(&runtime->rejected_count);
    }
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
    pc_metal_runtime_reset_observations();

    if (!acgc_metal_packet_consumer_register_runtime_callback(
            handoff,
            pc_metal_runtime_observe,
            &s_pc_metal_runtime
    )) {
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
        acgc_metal_sink_shutdown();
        return;
    }

    /* Stop new GX calls before clearing the consumer's borrowed callback. */
    pc_gx_clear_semantic_packet_v2_handoff();
    pc_gx_clear_semantic_packet_handoff();
    acgc_metal_packet_consumer_unregister_runtime_callback(
        &s_pc_metal_runtime.handoff
    );
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
