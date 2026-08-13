#include "acgc/gbi_runtime.h"
#include "acgc/graph_submission.h"
#include "sys_dynamic.h"

#include <libforest/gbi_extensions.h>
#include <PR/mbi.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

#define TEST_G_ENDDL_W0 UINT32_C(0xDF000000)
#define TEST_G_DL_BRANCH_W0 UINT32_C(0xDE010000)
#define TEST_G_NOOP_W0 UINT32_C(0xE7000000)
#define TEST_LIVE_TARGET_HANDLE UINT32_C(0xF0002000)
#define TEST_GFX_WORD_COUNT ((uint32_t)(sizeof(Gfx) / sizeof(uint32_t)))

typedef struct CaptureProbe {
    int calls;
    GraphTaskSubmissionCapture last_root;
    GraphTaskSubmissionTargetCapture last_target;
} CaptureProbe;

static void root_capture_callback(
    void* context,
    const GraphTaskSubmissionCapture* capture
) {
    CaptureProbe* probe = (CaptureProbe*)context;

    probe->calls++;
    probe->last_root = *capture;
}

static void target_capture_callback(
    void* context,
    const GraphTaskSubmissionTargetCapture* capture
) {
    CaptureProbe* probe = (CaptureProbe*)context;

    probe->calls++;
    probe->last_target = *capture;
}

static int test_sys_dynamic_new0_target(void) {
    const uint32_t root_word_capacity =
        (uint32_t)(sizeof(sys_dynamic.work) / sizeof(sys_dynamic.work[0])) *
        TEST_GFX_WORD_COUNT;
    const uint32_t target_word_capacity =
        (uint32_t)(sizeof(sys_dynamic.new0) / sizeof(sys_dynamic.new0[0])) *
        TEST_GFX_WORD_COUNT;
    CaptureProbe probe = { 0 };
    uintptr_t resolved_target = 0;
    uint32_t target_identity;
    uint32_t terminator_word_index;
    uint32_t i;
    AcgcGbiRuntimePtrStatus runtime_status;

    _Static_assert(
        sizeof(sys_dynamic.work) / sizeof(sys_dynamic.work[0]) *
            TEST_GFX_WORD_COUNT ==
            ACGC_GRAPH_SUBMISSION_CAPTURE_SOURCE_MAX_WORDS,
        "the root fixture must use the complete sys_dynamic.work span"
    );
    _Static_assert(
        sizeof(sys_dynamic.new0) / sizeof(sys_dynamic.new0[0]) *
            TEST_GFX_WORD_COUNT ==
            ACGC_GRAPH_SUBMISSION_CAPTURE_TARGET_MAX_WORDS,
        "the target fixture must use the complete sys_dynamic.new0 span"
    );

    memset(&sys_dynamic, 0, sizeof(sys_dynamic));
    pc_gbi_reset_runtime_ptr_registry();

    for (i = 0; i < 5; ++i) {
        sys_dynamic.new0[i].words.w0 = TEST_G_NOOP_W0;
        sys_dynamic.new0[i].words.w1 = 0;
    }
    gSPEndDisplayList(&sys_dynamic.new0[5]);
    gSPBranchList(sys_dynamic.work, sys_dynamic.new0);

    CHECK(sys_dynamic.work[0].words.w0 == TEST_G_DL_BRANCH_W0);
    CHECK(sys_dynamic.new0[5].words.w0 == TEST_G_ENDDL_W0);
    CHECK(sys_dynamic.new0[5].words.w1 == 0);
    target_identity = sys_dynamic.work[0].words.w1;

#if UINTPTR_MAX > UINT32_MAX
    CHECK(target_identity == TEST_LIVE_TARGET_HANDLE);
#endif
    runtime_status = pc_gbi_unpack_runtime_ptr(
        target_identity,
        &resolved_target
    );
#if UINTPTR_MAX > UINT32_MAX
    CHECK(runtime_status == ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(resolved_target == (uintptr_t)sys_dynamic.new0);
#else
    CHECK(runtime_status != ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    resolved_target = (uintptr_t)sys_dynamic.new0;
#endif

    graph_set_task_submission_capture_callback(
        root_capture_callback,
        &probe
    );
    graph_set_task_submission_target_capture_callback(
        target_capture_callback,
        &probe
    );

    graph_capture_task_submission(
        sys_dynamic.work,
        root_word_capacity,
        UINT32_C(73)
    );
    CHECK(probe.calls == 1);
    CHECK(probe.last_root.graph_frame == UINT32_C(73));
    CHECK(probe.last_root.source_word_capacity == root_word_capacity);
    CHECK(probe.last_root.captured_word_count ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS);
    CHECK(probe.last_root.classification ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_INDIRECT);
    CHECK(probe.last_root.terminator_word_index ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_NO_TERMINATOR);
    CHECK(probe.last_root.words[0] == TEST_G_DL_BRANCH_W0);
    CHECK(probe.last_root.words[1] == target_identity);

    graph_capture_task_submission_target(
        target_identity,
        (const void*)resolved_target,
        target_word_capacity,
        UINT32_C(73)
    );
    CHECK(probe.calls == 2);
    CHECK(probe.last_target.version ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_TARGET_VERSION);
    CHECK(probe.last_target.graph_frame == UINT32_C(73));
    CHECK(probe.last_target.target_identity == target_identity);
    CHECK(probe.last_target.target_word_capacity == target_word_capacity);
    CHECK(probe.last_target.captured_word_count ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS);
    CHECK(probe.last_target.classification ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_PREFIX_ONLY);
    CHECK(probe.last_target.terminator_word_index == 10);
    CHECK(probe.last_target.words[0] == TEST_G_NOOP_W0);

    CHECK(graph_classify_task_submission_target(
              sys_dynamic.new0,
              target_word_capacity,
              &terminator_word_index
          ) == ACGC_GRAPH_SUBMISSION_CAPTURE_COMPLETE);
    CHECK(terminator_word_index == 10);

    sys_dynamic.new0[5].words.w1 = 1;
    CHECK(graph_classify_task_submission_target(
              sys_dynamic.new0,
              target_word_capacity,
              NULL
          ) == ACGC_GRAPH_SUBMISSION_CAPTURE_MALFORMED);
    sys_dynamic.new0[5].words.w1 = 0;

    CHECK(graph_classify_task_submission_target(
              sys_dynamic.new0,
              2,
              NULL
          ) == ACGC_GRAPH_SUBMISSION_CAPTURE_UNTERMINATED);

    CHECK(graph_classify_task_submission_target(
              sys_dynamic.new0,
              target_word_capacity + 2,
              NULL
          ) == ACGC_GRAPH_SUBMISSION_CAPTURE_OVERSIZED);

    graph_clear_task_submission_target_capture_callback();
    graph_clear_task_submission_capture_callback();

    pc_gbi_reset_runtime_ptr_registry();
#if UINTPTR_MAX > UINT32_MAX
    CHECK(pc_gbi_unpack_runtime_ptr(target_identity, &resolved_target) ==
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(resolved_target == 0);
#endif
    CHECK(probe.last_target.target_identity == target_identity);
    CHECK(probe.last_target.target_word_capacity == target_word_capacity);
    CHECK(probe.last_target.words[0] == TEST_G_NOOP_W0);
    return 0;
}

int main(void) {
    CHECK(test_sys_dynamic_new0_target() == 0);
    puts("graph indirect target tests passed");
    return 0;
}
