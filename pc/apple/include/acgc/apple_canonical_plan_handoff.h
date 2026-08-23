#ifndef ACGC_APPLE_CANONICAL_PLAN_HANDOFF_H
#define ACGC_APPLE_CANONICAL_PLAN_HANDOFF_H

#include "acgc/apple_canonical_plan.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AcgcAppleCanonicalPlanHandoffResult {
    ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION = 0,
    ACGC_APPLE_CANONICAL_PLAN_HANDOFF_PLAN_REJECTED,
    ACGC_APPLE_CANONICAL_PLAN_HANDOFF_PLAN_PUBLISHED
} AcgcAppleCanonicalPlanHandoffResult;

/*
 * The plan pointer is borrowed only for the duration of this synchronous
 * callback. It is NULL for no-publication/rejected attempts. The callback is
 * same-owner and non-reentrant: it must not call GX init/shutdown, state
 * setters, GXBegin/GXEnd/flush, cumulative registration/clear, this handoff's
 * consumer registration/clear, or nested canonical consumption. It may copy
 * the value-owned plan before returning, but may not retain its address.
 */
typedef void (*AcgcAppleCanonicalPlanHandoffConsumer)(
    void* context,
    uint64_t attempt_id,
    AcgcAppleCanonicalPlanHandoffResult result,
    const AcgcAppleCanonicalPlan* plan
);

typedef struct AcgcAppleCanonicalPlanHandoffSnapshot {
    uint32_t callback_count;
    uint32_t publication_count;
    uint32_t rejected_build_count;
    uint32_t registered;
    uint32_t consumer_registered;
    /* Nonzero only while the current-attempt consumer is executing. */
    uint32_t plan_valid;
    uint64_t last_attempt_id;
    uint32_t attempt_count;
    AcgcAppleCanonicalPlanHandoffResult last_result;
    AcgcAppleCanonicalPlanStatus last_plan_status;
} AcgcAppleCanonicalPlanHandoffSnapshot;

/*
 * Idempotent registration against the existing synchronous GX callback.
 * The same GX owner must call this after pc_gx_init() and before the Apple
 * Metal runtime starts; it must not be called concurrently with GX work.
 */
int acgc_apple_canonical_plan_handoff_init(void);

/*
 * Clear the GX callback before invalidating the value-owned plan.  A failed
 * clear leaves registration, observations, and plan bytes intact for retry.
 * The same GX owner must call this after the Apple Metal runtime stops and
 * before pc_gx_shutdown().
 */
int acgc_apple_canonical_plan_handoff_shutdown(void);

/* Register the same-owner runtime consumer after the GX callback pair exists. */
int acgc_apple_canonical_plan_handoff_set_consumer(
    AcgcAppleCanonicalPlanHandoffConsumer consumer,
    void* context
);

/* Clear the borrowed consumer before runtime teardown. */
int acgc_apple_canonical_plan_handoff_clear_consumer(void);

/* Copy bounded observations without exposing a reusable plan. */
int acgc_apple_canonical_plan_handoff_get_snapshot(
    AcgcAppleCanonicalPlanHandoffSnapshot* snapshot
);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_APPLE_CANONICAL_PLAN_HANDOFF_H */
