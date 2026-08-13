#include "acgc/gbi_runtime.h"
#include "acgc/graph_submission.h"
#include "libforest/emu64/emu64_wrapper.h"
#include "sys_dynamic.h"

#include <libforest/gbi_extensions.h>
#include <PR/mbi.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern uint32_t pc_emu64_graph_frame;

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

typedef struct TargetCaptureProbe {
    int calls;
    GraphTaskSubmissionTargetCapture last_capture;
} TargetCaptureProbe;

static void target_capture_callback(
    void* context,
    const GraphTaskSubmissionTargetCapture* capture
) {
    TargetCaptureProbe* probe = (TargetCaptureProbe*)context;

    probe->calls++;
    probe->last_capture = *capture;
}

static int test_live_dl_target_capture(void) {
    const uint32_t target_word_capacity =
        (uint32_t)(sizeof(sys_dynamic.new0) / sizeof(sys_dynamic.new0[0])) *
        TEST_GFX_WORD_COUNT;
    TargetCaptureProbe probe = { 0 };
    uintptr_t resolved_target = 0;
    uint32_t target_identity;
    uint32_t i;

    _Static_assert(
        sizeof(sys_dynamic.new0) / sizeof(sys_dynamic.new0[0]) *
            TEST_GFX_WORD_COUNT ==
            ACGC_GRAPH_SUBMISSION_CAPTURE_TARGET_MAX_WORDS,
        "the live fixture must use the complete sys_dynamic.new0 span"
    );
    CHECK(target_word_capacity == UINT32_C(1024));

#if UINTPTR_MAX <= UINT32_MAX
    puts("pc_live_graph_target_capture_fixture: SKIP (requires a wider-than-u32 uintptr_t)");
    return 77;
#else
    CHECK((uintptr_t)sys_dynamic.new0 > (uintptr_t)UINT32_MAX);

    memset(&sys_dynamic, 0, sizeof(sys_dynamic));
    pc_gbi_reset_runtime_ptr_registry();

    for (i = 0; i < 5; ++i) {
        sys_dynamic.new0[i].words.w0 = TEST_G_NOOP_W0;
        sys_dynamic.new0[i].words.w1 = 0;
    }
    gSPEndDisplayList(&sys_dynamic.new0[5]);
    gSPBranchList(&sys_dynamic.work[0], sys_dynamic.new0);
    gSPEndDisplayList(&sys_dynamic.work[1]);

    CHECK(sys_dynamic.work[0].words.w0 == TEST_G_DL_BRANCH_W0);
    CHECK(sys_dynamic.new0[5].words.w0 == TEST_G_ENDDL_W0);
    CHECK(sys_dynamic.new0[5].words.w1 == 0);
    target_identity = sys_dynamic.work[0].words.w1;
    CHECK(target_identity == TEST_LIVE_TARGET_HANDLE);
    CHECK(pc_gbi_unpack_runtime_ptr(target_identity, &resolved_target) ==
          ACGC_GBI_RUNTIME_PTR_RESOLVED);
    CHECK(resolved_target == (uintptr_t)sys_dynamic.new0);

    pc_emu64_graph_frame = UINT32_C(73);
    graph_set_task_submission_target_capture_callback(
        target_capture_callback,
        &probe
    );

    emu64_init();
    emu64_taskstart(sys_dynamic.work);

    CHECK(probe.calls == 1);
    CHECK(probe.last_capture.version ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_TARGET_VERSION);
    CHECK(probe.last_capture.graph_frame == UINT32_C(73));
    CHECK(probe.last_capture.target_identity == target_identity);
    CHECK(probe.last_capture.target_word_capacity == target_word_capacity);
    CHECK(probe.last_capture.captured_word_count ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS);
    CHECK(probe.last_capture.classification ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_PREFIX_ONLY);
    CHECK(probe.last_capture.terminator_word_index == UINT32_C(10));
    /* The fixed snapshot is eight words; the exact terminator is outside the
       copied prefix and is proven by terminator_word_index plus the source
       assertion above. */
    CHECK(probe.last_capture.words[0] == TEST_G_NOOP_W0);
    CHECK(probe.last_capture.words[1] == 0);
    CHECK(probe.last_capture.words[2] == TEST_G_NOOP_W0);
    CHECK(probe.last_capture.words[3] == 0);
    CHECK(probe.last_capture.words[4] == TEST_G_NOOP_W0);
    CHECK(probe.last_capture.words[5] == 0);
    CHECK(probe.last_capture.words[6] == TEST_G_NOOP_W0);
    CHECK(probe.last_capture.words[7] == 0);

    CHECK(pc_gbi_unpack_runtime_ptr(target_identity, &resolved_target) ==
          ACGC_GBI_RUNTIME_PTR_INVALID_REFERENCE);
    CHECK(resolved_target == 0);

    /* The root still contains the old opaque word, but the registry has been
       reset by emu64_taskstart. A stale handle must fail before observation. */
    probe.calls = 0;
    emu64_init();
    emu64_taskstart(sys_dynamic.work);
    CHECK(probe.calls == 0);

    graph_clear_task_submission_target_capture_callback();
    return 0;
#endif
}

int main(void) {
    int result = test_live_dl_target_capture();

    if (result == 77) {
        return 77;
    }
    CHECK(result == 0);
    puts("pc_live_graph_target_capture_fixture: PASS"
         " (live F0002000 target resolves, captures the bounded new0 span,"
         " and stale handles fail closed)");
    return 0;
}
