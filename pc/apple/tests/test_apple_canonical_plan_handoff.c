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

static TestCumulativeSnapshotCallback s_callback;
static void* s_callback_context;
static int s_register_result = 1;
static int s_clear_result = 1;
static uint32_t s_register_calls;
static uint32_t s_clear_calls;
static uint32_t s_build_calls;
static AcgcAppleCanonicalPlanStatus s_build_status =
    ACGC_APPLE_CANONICAL_PLAN_OK;
static AcgcAppleCanonicalPlan s_plan_template;

int pc_gx_set_cumulative_snapshot_callback(
    TestCumulativeSnapshotCallback callback,
    void* context
) {
    s_register_calls++;
    if (!s_register_result) {
        return 0;
    }
    s_callback = callback;
    s_callback_context = context;
    return 1;
}

int pc_gx_clear_cumulative_snapshot_callback(void) {
    s_clear_calls++;
    if (!s_clear_result) {
        return 0;
    }
    s_callback = NULL;
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

static int snapshot_is(
    const AcgcAppleCanonicalPlanHandoffSnapshot* snapshot,
    uint32_t callback_count,
    uint32_t publication_count,
    uint32_t rejected_build_count,
    uint32_t registered,
    uint32_t plan_valid,
    AcgcAppleCanonicalPlanStatus last_plan_status
) {
    return snapshot != NULL &&
        snapshot->callback_count == callback_count &&
        snapshot->publication_count == publication_count &&
        snapshot->rejected_build_count == rejected_build_count &&
        snapshot->registered == registered &&
        snapshot->plan_valid == plan_valid &&
        snapshot->last_plan_status == last_plan_status;
}

static int run_tests(void) {
    const uint8_t envelope[] = { 0xAC, 0x0C, 0x01, 0x00 };
    AcgcAppleCanonicalPlanHandoffSnapshot snapshot;
    AcgcAppleCanonicalPlan published_plan;
    AcgcAppleCanonicalPlan output;
    AcgcAppleCanonicalPlan output_before;
    TestCumulativeSnapshotCallback captured_callback;
    void* captured_context;
    uint32_t build_calls_before;
    int wrong_context;

    /* The literal is only a nonempty synchronous stub payload. */
    (void)envelope;
    memset(&s_plan_template, 0x5A, sizeof(s_plan_template));
    s_plan_template.geometry.vertex_count = 3;

    CHECK(acgc_apple_canonical_plan_handoff_shutdown());
    s_register_result = 0;
    CHECK(!acgc_apple_canonical_plan_handoff_init());
    CHECK(s_register_calls == 1);
    CHECK(s_callback == NULL);
    CHECK(s_callback_context == NULL);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 0, 0, 0, 0, 0,
        ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT));
    s_register_result = 1;
    CHECK(acgc_apple_canonical_plan_handoff_init());
    CHECK(s_register_calls == 2);
    CHECK(s_callback != NULL);
    CHECK(s_callback_context != NULL);
    captured_callback = s_callback;
    captured_context = s_callback_context;

    CHECK(acgc_apple_canonical_plan_handoff_init());
    CHECK(s_register_calls == 2);
    CHECK(s_callback == captured_callback);
    CHECK(s_callback_context == captured_context);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 0, 0, 0, 1, 0,
        ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT));
    CHECK(!acgc_apple_canonical_plan_handoff_get_snapshot(NULL));
    CHECK(!acgc_apple_canonical_plan_handoff_copy_plan(NULL));

    memset(&output, 0xA5, sizeof(output));
    output_before = output;
    CHECK(!acgc_apple_canonical_plan_handoff_copy_plan(&output));
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);

    wrong_context = 0;
    captured_callback(NULL, envelope, sizeof(envelope));
    captured_callback(&wrong_context, envelope, sizeof(envelope));
    CHECK(s_build_calls == 0);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 0, 0, 0, 1, 0,
        ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT));

    s_build_status = ACGC_APPLE_CANONICAL_PLAN_OK;
    captured_callback(captured_context, envelope, sizeof(envelope));
    CHECK(s_build_calls == 1);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 1, 1, 0, 1, 1,
        ACGC_APPLE_CANONICAL_PLAN_OK));
    CHECK(acgc_apple_canonical_plan_handoff_copy_plan(&published_plan));
    CHECK(memcmp(
        &published_plan, &s_plan_template, sizeof(published_plan)) == 0);

    s_build_status = ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC;
    captured_callback(captured_context, envelope, sizeof(envelope));
    CHECK(s_build_calls == 2);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 2, 1, 1, 1, 1,
        ACGC_APPLE_CANONICAL_PLAN_SECTION_SEMANTIC));
    CHECK(acgc_apple_canonical_plan_handoff_copy_plan(&output));
    CHECK(memcmp(&output, &published_plan, sizeof(output)) == 0);

    s_build_status = ACGC_APPLE_CANONICAL_PLAN_OK;
    captured_callback(captured_context, NULL, 0);
    CHECK(s_build_calls == 3);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 3, 1, 2, 1, 1,
        ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT));
    CHECK(acgc_apple_canonical_plan_handoff_copy_plan(&output));
    CHECK(memcmp(&output, &published_plan, sizeof(output)) == 0);

    s_clear_result = 0;
    CHECK(!acgc_apple_canonical_plan_handoff_shutdown());
    CHECK(s_clear_calls == 1);
    CHECK(s_callback == captured_callback);
    CHECK(s_callback_context == captured_context);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 3, 1, 2, 1, 1,
        ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT));
    CHECK(acgc_apple_canonical_plan_handoff_copy_plan(&output));
    CHECK(memcmp(&output, &published_plan, sizeof(output)) == 0);

    s_clear_result = 1;
    CHECK(acgc_apple_canonical_plan_handoff_shutdown());
    CHECK(s_clear_calls == 2);
    CHECK(s_callback == NULL);
    CHECK(s_callback_context == NULL);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 0, 0, 0, 0, 0,
        ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT));
    memset(&output, 0xA5, sizeof(output));
    output_before = output;
    CHECK(!acgc_apple_canonical_plan_handoff_copy_plan(&output));
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);

    build_calls_before = s_build_calls;
    captured_callback(captured_context, envelope, sizeof(envelope));
    CHECK(s_build_calls == build_calls_before);

    CHECK(acgc_apple_canonical_plan_handoff_init());
    CHECK(s_register_calls == 3);
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 0, 0, 0, 1, 0,
        ACGC_APPLE_CANONICAL_PLAN_INVALID_ARGUMENT));
    s_build_status = ACGC_APPLE_CANONICAL_PLAN_OK;
    CHECK(s_callback != NULL);
    s_callback(s_callback_context, envelope, sizeof(envelope));
    CHECK(acgc_apple_canonical_plan_handoff_get_snapshot(&snapshot));
    CHECK(snapshot_is(
        &snapshot, 1, 1, 0, 1, 1,
        ACGC_APPLE_CANONICAL_PLAN_OK));
    CHECK(acgc_apple_canonical_plan_handoff_shutdown());
    return 1;
}

int main(void) {
    return run_tests() ?
        (puts("Apple canonical plan handoff tests: PASS"), 0) : 1;
}
