#include "acgc/apple_canonical_plan_handoff.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Keep the Apple handoff independent of pc_gx_internal.h.  That internal
 * header pulls the SDL/OpenGL-backed GX state into this portable C component
 * and prevents the standalone Apple fixture from being a pure handoff test.
 * These declarations intentionally reproduce the public callback ABI from
 * pc/include/pc_gx_cumulative_gatherer.h exactly; the GX owner remains
 * responsible for callback storage and synchronous envelope lifetime.  Any
 * future ABI change must update this adapter and its fixture together before
 * production integration.
 */
typedef void (*AcgcAppleCanonicalPlanHandoffCallback)(
    void* context,
    const uint8_t* envelope,
    size_t envelope_byte_size
);

extern int pc_gx_set_cumulative_snapshot_callback(
    AcgcAppleCanonicalPlanHandoffCallback callback,
    void* context
);
extern int pc_gx_clear_cumulative_snapshot_callback(void);

typedef struct AcgcAppleCanonicalPlanHandoffContext {
    AcgcAppleCanonicalPlan plan;
    uint32_t callback_count;
    uint32_t publication_count;
    uint32_t rejected_build_count;
    AcgcAppleCanonicalPlanStatus last_plan_status;
    uint32_t registered;
    uint32_t plan_valid;
} AcgcAppleCanonicalPlanHandoffContext;

static AcgcAppleCanonicalPlanHandoffContext s_handoff = {
    .last_plan_status = ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT
};

static void increment_saturated(uint32_t* counter) {
    if (counter != NULL && *counter != UINT32_MAX) {
        *counter += UINT32_C(1);
    }
}

static void reset_after_clear(void) {
    memset(&s_handoff.plan, 0, sizeof(s_handoff.plan));
    s_handoff.callback_count = 0;
    s_handoff.publication_count = 0;
    s_handoff.rejected_build_count = 0;
    s_handoff.last_plan_status = ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT;
    s_handoff.registered = 0;
    s_handoff.plan_valid = 0;
}

static void acgc_apple_canonical_plan_handoff_callback(
    void* context,
    const uint8_t* envelope,
    size_t envelope_byte_size
) {
    AcgcAppleCanonicalPlanStatus status;

    if (context != &s_handoff || s_handoff.registered == 0) {
        return;
    }

    increment_saturated(&s_handoff.callback_count);
    status = acgc_apple_canonical_plan_build(
        envelope,
        envelope_byte_size,
        &s_handoff.plan
    );
    s_handoff.last_plan_status = status;
    if (status == ACGC_APPLE_CANONICAL_PLAN_OK) {
        s_handoff.plan_valid = 1;
        increment_saturated(&s_handoff.publication_count);
    } else {
        /* The plan builder guarantees output immutability on failure. */
        increment_saturated(&s_handoff.rejected_build_count);
    }
}

int acgc_apple_canonical_plan_handoff_init(void) {
    if (s_handoff.registered != 0) {
        return 1;
    }
    if (!pc_gx_set_cumulative_snapshot_callback(
            acgc_apple_canonical_plan_handoff_callback,
            &s_handoff
        )) {
        return 0;
    }
    s_handoff.registered = 1;
    return 1;
}

int acgc_apple_canonical_plan_handoff_shutdown(void) {
    if (s_handoff.registered == 0) {
        return 1;
    }
    if (!pc_gx_clear_cumulative_snapshot_callback()) {
        return 0;
    }
    reset_after_clear();
    return 1;
}

int acgc_apple_canonical_plan_handoff_get_snapshot(
    AcgcAppleCanonicalPlanHandoffSnapshot* snapshot
) {
    AcgcAppleCanonicalPlanHandoffSnapshot candidate;

    if (snapshot == NULL) {
        return 0;
    }
    candidate.callback_count = s_handoff.callback_count;
    candidate.publication_count = s_handoff.publication_count;
    candidate.rejected_build_count = s_handoff.rejected_build_count;
    candidate.registered = s_handoff.registered;
    candidate.plan_valid = s_handoff.plan_valid;
    candidate.last_plan_status = s_handoff.last_plan_status;
    *snapshot = candidate;
    return 1;
}

int acgc_apple_canonical_plan_handoff_copy_plan(
    AcgcAppleCanonicalPlan* destination
) {
    if (destination == NULL || s_handoff.registered == 0 ||
        s_handoff.plan_valid == 0) {
        return 0;
    }
    *destination = s_handoff.plan;
    return 1;
}
