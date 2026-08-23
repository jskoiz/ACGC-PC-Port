#include "acgc/apple_canonical_plan_handoff.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef void (*TestCumulativeSnapshotCallback)(
    void* context,
    const uint8_t* envelope,
    size_t envelope_byte_size
);
typedef void (*TestCumulativeSnapshotAttemptCallback)(
    void* context,
    uint64_t attempt_id,
    int result
);

static TestCumulativeSnapshotCallback s_callback;
static TestCumulativeSnapshotAttemptCallback s_attempt_callback;
static void* s_callback_context;
static int s_register_result = 1;
static int s_clear_result = 1;
static uint32_t s_register_calls;
static uint32_t s_clear_calls;
static uint32_t s_build_calls;
static AcgcAppleCanonicalPlanStatus s_build_status =
    ACGC_APPLE_CANONICAL_PLAN_OK;
static AcgcAppleCanonicalPlan s_plan_template;
static uint32_t s_consumer_calls;
static uint64_t s_consumer_attempt_id;
static AcgcAppleCanonicalPlanHandoffResult s_consumer_result;
static AcgcAppleCanonicalPlan s_consumed_plan;
static int s_consumer_received_plan;
static int s_consumer_reenter;
static int s_reentry_init_result;
static int s_reentry_shutdown_result;
static int s_reentry_set_result;
static int s_reentry_clear_result;

int pc_gx_set_cumulative_snapshot_callbacks(
    TestCumulativeSnapshotCallback callback,
    TestCumulativeSnapshotAttemptCallback attempt_callback,
    void* context
) {
    s_register_calls++;
    if (!s_register_result) {
        return 0;
    }
    s_callback = callback;
    s_attempt_callback = attempt_callback;
    s_callback_context = context;
    return 1;
}

int pc_gx_clear_cumulative_snapshot_callbacks(void) {
    s_clear_calls++;
    if (!s_clear_result) {
        return 0;
    }
    s_callback = NULL;
    s_attempt_callback = NULL;
    s_callback_context = NULL;
    return 1;
}

AcgcAppleCanonicalPlanStatus acgc_apple_canonical_plan_build(
    const uint8_t* envelope_bytes,
    size_t envelope_byte_size,
    AcgcAppleCanonicalPlan* output
) {
    s_build_calls++;
    if (s_build_status != ACGC_APPLE_CANONICAL_PLAN_OK) {
        return s_build_status;
    }
    if (envelope_bytes == NULL || envelope_byte_size == 0 || output == NULL) {
        return ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT;
    }
    *output = s_plan_template;
    return ACGC_APPLE_CANONICAL_PLAN_OK;
}

static void test_consumer(
    void* context,
    uint64_t attempt_id,
    AcgcAppleCanonicalPlanHandoffResult result,
    const AcgcAppleCanonicalPlan* plan
) {
    (void)context;
    s_consumer_calls++;
    s_consumer_attempt_id = attempt_id;
    s_consumer_result = result;
    s_consumer_received_plan = plan != NULL;
    if (plan != NULL) {
        s_consumed_plan = *plan;
    }
    if (s_consumer_reenter) {
        s_reentry_init_result = acgc_apple_canonical_plan_handoff_init();
        s_reentry_shutdown_result =
            acgc_apple_canonical_plan_handoff_shutdown();
        s_reentry_set_result = acgc_apple_canonical_plan_handoff_set_consumer(
            test_consumer,
            NULL
        );
        s_reentry_clear_result =
            acgc_apple_canonical_plan_handoff_clear_consumer();
    }
}

static int snapshot_is(
    const AcgcAppleCanonicalPlanHandoffSnapshot* snapshot,
    uint32_t callback_count,
    uint32_t publication_count,
    uint32_t rejected_build_count,
    uint32_t registered,
    uint32_t consumer_registered,
    uint32_t plan_valid,
    uint64_t last_attempt_id,
    uint32_t attempt_count,
    AcgcAppleCanonicalPlanHandoffResult last_result,
    AcgcAppleCanonicalPlanStatus last_plan_status
) {
    return snapshot != NULL &&
        snapshot->callback_count == callback_count &&
        snapshot->publication_count == publication_count &&
        snapshot->rejected_build_count == rejected_build_count &&
        snapshot->registered == registered &&
        snapshot->consumer_registered == consumer_registered &&
        snapshot->plan_valid == plan_valid &&
        snapshot->last_attempt_id == last_attempt_id &&
        snapshot->attempt_count == attempt_count &&
        snapshot->last_result == last_result &&
        snapshot->last_plan_status == last_plan_status;
}

static int run_tests(void) {
    const uint8_t envelope[] = { 0xAC, 0x0C, 0x01, 0x00 };
    AcgcAppleCanonicalPlanHandoffSnapshot snapshot;
    TestCumulativeSnapshotCallback captured_callback;
    TestCumulativeSnapshotAttemptCallback captured_attempt_callback;
    void* captured_context;
    uint32_t build_calls_before;
    uint32_t consumer_calls_before;
    int wrong_context;

    memset(&s_plan_template, 0x5A, sizeof(s_plan_template));
    s_plan_template.geometry.vertex_count = 3;

    CHECK(acgc_apple_canonical_plan_handoff_shutdown());
    s_register_result = 0;
    CHECK(!acgc_apple_canonical_plan_handoff_init());
    CHECK(s_register_calls == 1);
    CHECK(s_callback == NULL);
    CHECK(s_attempt_callback == NULL);
    CHECK(s_callback_context == NULL);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 0, 0, 0, 0, 0, 0, 0, 0,
        ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION,
        ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT));

    s_register_result = 1;
    CHECK(acgc_apple_canonical_plan_handoff_init());
    CHECK(s_register_calls == 2);
    CHECK(s_callback != NULL);
    CHECK(s_attempt_callback != NULL);
    CHECK(s_callback_context != NULL);
    captured_callback = s_callback;
    captured_attempt_callback = s_attempt_callback;
    captured_context = s_callback_context;

    CHECK(acgc_apple_canonical_plan_handoff_init());
    CHECK(s_register_calls == 2);
    CHECK(s_callback == captured_callback);
    CHECK(s_attempt_callback == captured_attempt_callback);
    CHECK(s_callback_context == captured_context);
    CHECK(acgc_apple_canonical_plan_handoff_set_consumer(
        test_consumer,
        NULL
    ));
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 0, 0, 0, 1, 1, 0, 0, 0,
        ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION,
        ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT));
    CHECK(!acgc_apple_canonical_plan_handoff_get_snapshot(NULL));

    wrong_context = 0;
    captured_callback(NULL, envelope, sizeof(envelope));
    captured_callback(&wrong_context, envelope, sizeof(envelope));
    CHECK(s_build_calls == 0);

    s_build_status = ACGC_APPLE_CANONICAL_PLAN_OK;
    captured_callback(captured_context, envelope, sizeof(envelope));
    CHECK(s_build_calls == 1);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 1, 0, 0, 1, 1, 0, 0, 0,
        ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION,
        ACGC_APPLE_CANONICAL_PLAN_OK));
    captured_attempt_callback(captured_context, 1, 1);
    CHECK(s_consumer_calls == 1);
    CHECK(s_consumer_attempt_id == 1);
    CHECK(s_consumer_result ==
          ACGC_APPLE_CANONICAL_PLAN_HANDOFF_PLAN_PUBLISHED);
    CHECK(s_consumer_received_plan);
    CHECK(memcmp(&s_consumed_plan, &s_plan_template, sizeof(s_consumed_plan)) == 0);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 1, 1, 0, 1, 1, 0, 1, 1,
        ACGC_APPLE_CANONICAL_PLAN_HANDOFF_PLAN_PUBLISHED,
        ACGC_APPLE_CANONICAL_PLAN_OK));

    s_build_status = ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    captured_callback(captured_context, envelope, sizeof(envelope));
    CHECK(s_build_calls == 2);
    captured_attempt_callback(captured_context, 2, 1);
    CHECK(s_consumer_calls == 2);
    CHECK(s_consumer_attempt_id == 2);
    CHECK(s_consumer_result ==
          ACGC_APPLE_CANONICAL_PLAN_HANDOFF_PLAN_REJECTED);
    CHECK(!s_consumer_received_plan);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 2, 1, 1, 1, 1, 0, 2, 2,
        ACGC_APPLE_CANONICAL_PLAN_HANDOFF_PLAN_REJECTED,
        ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC));

    captured_attempt_callback(captured_context, 3, 0);
    CHECK(s_consumer_calls == 3);
    CHECK(s_consumer_result ==
          ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 2, 1, 1, 1, 1, 0, 3, 3,
        ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION,
        ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT));

    /* Duplicate/stale tokens invalidate the consumer view but do not advance
     * the current-attempt diagnostic counters. */
    consumer_calls_before = s_consumer_calls;
    captured_attempt_callback(captured_context, 2, 1);
    CHECK(s_consumer_calls == consumer_calls_before + 1);
    CHECK(s_consumer_result ==
          ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION);
    CHECK(!s_consumer_received_plan);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot.last_attempt_id == 3);
    CHECK(snapshot.attempt_count == 3);
    CHECK(snapshot.plan_valid == 0);

    /* Reentry during a borrowed plan is rejected, and normal teardown still
     * clears the consumer and callback pair afterward. */
    s_build_status = ACGC_APPLE_CANONICAL_PLAN_OK;
    s_consumer_reenter = 1;
    captured_callback(captured_context, envelope, sizeof(envelope));
    captured_attempt_callback(captured_context, 4, 1);
    s_consumer_reenter = 0;
    CHECK(s_reentry_init_result == 0);
    CHECK(s_reentry_shutdown_result == 0);
    CHECK(s_reentry_set_result == 0);
    CHECK(s_reentry_clear_result == 0);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot.plan_valid == 0);
    CHECK(snapshot.last_result ==
          ACGC_APPLE_CANONICAL_PLAN_HANDOFF_PLAN_PUBLISHED);

    s_clear_result = 0;
    CHECK(!acgc_apple_canonical_plan_handoff_shutdown());
    CHECK(s_clear_calls == 1);
    CHECK(s_callback == captured_callback);
    CHECK(s_attempt_callback == captured_attempt_callback);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot.registered == 1);
    CHECK(snapshot.consumer_registered == 1);

    s_clear_result = 1;
    CHECK(acgc_apple_canonical_plan_handoff_shutdown());
    CHECK(s_clear_calls == 2);
    CHECK(s_callback == NULL);
    CHECK(s_attempt_callback == NULL);
    CHECK(s_callback_context == NULL);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 0, 0, 0, 0, 0, 0, 0, 0,
        ACGC_APPLE_CANONICAL_PLAN_HANDOFF_NO_PUBLICATION,
        ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT));

    build_calls_before = s_build_calls;
    captured_callback(captured_context, envelope, sizeof(envelope));
    CHECK(s_build_calls == build_calls_before);

    CHECK(acgc_apple_canonical_plan_handoff_init());
    CHECK(s_register_calls == 3);
    CHECK(acgc_apple_canonical_plan_handoff_set_consumer(
        test_consumer,
        NULL
    ));
    CHECK(s_callback != NULL);
    CHECK(s_attempt_callback != NULL);
    s_build_status = ACGC_APPLE_CANONICAL_PLAN_OK;
    s_consumer_reenter = 0;
    s_consumer_received_plan = 0;
    s_callback(s_callback_context, envelope, sizeof(envelope));
    s_attempt_callback(s_callback_context, 5, 1);
    CHECK(s_consumer_result ==
          ACGC_APPLE_CANONICAL_PLAN_HANDOFF_PLAN_PUBLISHED);
    CHECK(s_consumer_received_plan);
    CHECK(acgc_apple_canonical_plan_handoff_shutdown());
    return 1;
}

int main(void) {
    return run_tests() ?
        (puts("Apple canonical plan handoff tests: PASS"), 0) : 1;
}
