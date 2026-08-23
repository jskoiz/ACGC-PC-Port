#ifndef ACGC_APPLE_CANONICAL_PLAN_HANDOFF_H
#define ACGC_APPLE_CANONICAL_PLAN_HANDOFF_H

#include "acgc/apple_canonical_plan.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * These observations are a bounded, same-owner diagnostic copy.  They do not
 * make the value-owned plan concurrently readable; consumers must use the
 * copy API from the owner that drives the synchronous callback.
 */
typedef struct AcgcAppleCanonicalPlanHandoffSnapshot {
    uint32_t callback_count;
    uint32_t publication_count;
    uint32_t rejected_build_count;
    uint32_t registered;
    uint32_t plan_valid;
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

/* Copy bounded observations without exposing the callback context or plan. */
int acgc_apple_canonical_plan_handoff_get_snapshot(
    AcgcAppleCanonicalPlanHandoffSnapshot* snapshot
);

/* Copy the last successfully published plan; failure leaves destination intact. */
int acgc_apple_canonical_plan_handoff_copy_plan(
    AcgcAppleCanonicalPlan* destination
);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_APPLE_CANONICAL_PLAN_HANDOFF_H */
