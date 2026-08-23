#include "acgc/apple_canonical_plan_handoff.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Keep this Apple adapter independent of pc_gx_internal.h.  These declarations
 * reproduce the public callback ABI from pc_gx_cumulative_gatherer.h; `int` is
 * used for the result parameter so this standalone adapter does not need to
 * import the SDL/OpenGL-backed PC internal header.
 */
typedef void (*AcgcAppleCanonicalPlanHandoffCallback)(
    void* context,
    const uint8_t* envelope,
    size_t envelope_byte_size
);
typedef void (*AcgcAppleCanonicalPlanHandoffAttemptCallback)(
    void* context,
    uint64_t attempt_id,
    int result
);

extern int pc_gx_set_cumulative_snapshot_callbacks(
    AcgcAppleCanonicalPlanHandoffCallback callback,
    AcgcAppleCanonicalPlanHandoffAttemptCallback attempt_callback,
    void* context
);
extern int pc_gx_clear_cumulative_snapshot_callbacks(void);

typedef struct AcgcAppleCanonicalPlanHandoffContext {
    AcgcAppleCanonicalPlan current_plan;
    AcgcAppleCanonicalPlan pending_plan;
    uint64_t last_attempt_id;
    uint32_t have_attempt_id;
    uint32_t attempt_count;
    uint32_t callback_count;
    uint32_t publication_count;
    uint32_t rejected_build_count;
    uint32_t registered;
    uint32_t consumer_registered;
    uint32_t plan_valid;
    uint32_t pending_plan_valid;
    uint32_t callback_active;
    AcgcAppleCanonicalPlanHandoffResult last_result;
    AcgcAppleCanonicalPlanStatus last_plan_status;
    AcgcAppleCanonicalPlanHandoffConsumer consumer;
    void* consumer_context;
} AcgcAppleCanonicalPlanHandoffContext;

static AcgcAppleCanonicalPlanHandoffContext s_handoff = {
    .last_result = ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION,
    .last_plan_status = ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT
};

static void increment_saturated(uint32_t* counter) {
    if (counter != NULL && *counter != UINT32_MAX) {
        *counter += UINT32_C(1);
    }
}

static void clear_plan_state(void) {
    memset(&s_handoff.current_plan, 0, sizeof(s_handoff.current_plan));
    memset(&s_handoff.pending_plan, 0, sizeof(s_handoff.pending_plan));
    s_handoff.plan_valid = 0;
    s_handoff.pending_plan_valid = 0;
}

static void reset_after_clear(void) {
    memset(&s_handoff, 0, sizeof(s_handoff));
    s_handoff.last_result =
        ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION;
    s_handoff.last_plan_status = ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT;
}

static void acgc_apple_canonical_plan_handoff_callback(
    void* context,
    const uint8_t* envelope,
    size_t envelope_byte_size
) {
    AcgcAppleCanonicalPlan candidate;
    AcgcAppleCanonicalPlanStatus status;

    if (context != &s_handoff || s_handoff.registered == 0 ||
        s_handoff.callback_active != 0) {
        return;
    }

    s_handoff.callback_active = 1;
    increment_saturated(&s_handoff.callback_count);
    /* A new envelope is a new attempt candidate; never leave the old plan
     * available while this synchronous transaction is being completed. */
    clear_plan_state();
    memset(&candidate, 0, sizeof(candidate));
    status = acgc_apple_canonical_plan_build(
        envelope,
        envelope_byte_size,
        &candidate
    );
    s_handoff.last_plan_status = status;
    if (status == ACGC_APPLE_CANONICAL_PLAN_OK) {
        s_handoff.pending_plan = candidate;
        s_handoff.pending_plan_valid = 1;
    }
    s_handoff.callback_active = 0;
}

static void acgc_apple_canonical_plan_handoff_attempt_callback(
    void* context,
    uint64_t attempt_id,
    int result
) {
    AcgcAppleCanonicalPlanHandoffResult handoff_result;
    AcgcAppleCanonicalPlanHandoffConsumer consumer;
    void* consumer_context;
    int is_fresh;

    if (context != &s_handoff || s_handoff.registered == 0 ||
        s_handoff.callback_active != 0 || attempt_id == 0) {
        return;
    }

    is_fresh = s_handoff.have_attempt_id == 0 ||
        attempt_id > s_handoff.last_attempt_id;
    if (!is_fresh) {
        /* A duplicate/stale event cannot publish an old plan. Still deliver a
         * no-publication invalidation so a consumer cannot leave its winner
         * latched on a stale token. */
        s_handoff.callback_active = 1;
        clear_plan_state();
        consumer = s_handoff.consumer;
        consumer_context = s_handoff.consumer_context;
        if (consumer != NULL) {
            consumer(
                consumer_context,
                attempt_id,
                ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION,
                NULL
            );
        }
        clear_plan_state();
        s_handoff.callback_active = 0;
        return;
    }

    s_handoff.callback_active = 1;
    s_handoff.have_attempt_id = 1;
    s_handoff.last_attempt_id = attempt_id;
    increment_saturated(&s_handoff.attempt_count);

    if (result == 1 && s_handoff.pending_plan_valid != 0) {
        handoff_result =
            ACGC_APPLE_CANONICAL_PLAN_HANDOFF_PLAN_PUBLISHED;
        s_handoff.current_plan = s_handoff.pending_plan;
        s_handoff.plan_valid = 1;
        increment_saturated(&s_handoff.publication_count);
    } else if (result == 1) {
        handoff_result = ACGC_APPLE_CANONICAL_PLAN_HANDOFF_PLAN_REJECTED;
        increment_saturated(&s_handoff.rejected_build_count);
        clear_plan_state();
    } else {
        handoff_result =
            ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION;
        clear_plan_state();
        s_handoff.last_plan_status = ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT;
    }

    s_handoff.last_result = handoff_result;
    s_handoff.pending_plan_valid = 0;
    memset(&s_handoff.pending_plan, 0, sizeof(s_handoff.pending_plan));
    consumer = s_handoff.consumer;
    consumer_context = s_handoff.consumer_context;
    if (consumer != NULL) {
        consumer(
            consumer_context,
            attempt_id,
            handoff_result,
            handoff_result ==
                    ACGC_APPLE_CANONICAL_PLAN_HANDOFF_PLAN_PUBLISHED
                ? &s_handoff.current_plan
                : NULL
        );
    }

    /* The plan was borrowed only during the consumer call. */
    clear_plan_state();
    s_handoff.callback_active = 0;
}

int acgc_apple_canonical_plan_handoff_init(void) {
    if (s_handoff.callback_active != 0 || s_handoff.registered != 0) {
        return s_handoff.callback_active == 0 ? 1 : 0;
    }
    if (!pc_gx_set_cumulative_snapshot_callbacks(
            acgc_apple_canonical_plan_handoff_callback,
            acgc_apple_canonical_plan_handoff_attempt_callback,
            &s_handoff
        )) {
        return 0;
    }
    s_handoff.registered = 1;
    return 1;
}

int acgc_apple_canonical_plan_handoff_shutdown(void) {
    if (s_handoff.callback_active != 0) {
        return 0;
    }
    if (s_handoff.registered == 0) {
        return 1;
    }
    if (!pc_gx_clear_cumulative_snapshot_callbacks()) {
        return 0;
    }
    reset_after_clear();
    return 1;
}

int acgc_apple_canonical_plan_handoff_set_consumer(
    AcgcAppleCanonicalPlanHandoffConsumer consumer,
    void* context
) {
    if (consumer == NULL || s_handoff.registered == 0 ||
        s_handoff.callback_active != 0) {
        return 0;
    }
    if (s_handoff.consumer_registered != 0) {
        return s_handoff.consumer == consumer &&
            s_handoff.consumer_context == context;
    }
    s_handoff.consumer = consumer;
    s_handoff.consumer_context = context;
    s_handoff.consumer_registered = 1;
    return 1;
}

int acgc_apple_canonical_plan_handoff_clear_consumer(void) {
    if (s_handoff.callback_active != 0) {
        return 0;
    }
    s_handoff.consumer = NULL;
    s_handoff.consumer_context = NULL;
    s_handoff.consumer_registered = 0;
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
    candidate.consumer_registered = s_handoff.consumer_registered;
    candidate.plan_valid = s_handoff.plan_valid;
    candidate.last_attempt_id = s_handoff.last_attempt_id;
    candidate.attempt_count = s_handoff.attempt_count;
    candidate.last_result = s_handoff.last_result;
    candidate.last_plan_status = s_handoff.last_plan_status;
    *snapshot = candidate;
    return 1;
}
