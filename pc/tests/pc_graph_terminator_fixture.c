#include "acgc/graph_submission.h"

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
#define TEST_G_NOOP_W0 UINT32_C(0xE7000000)
#define TEST_TARGET_ID UINT32_C(0xF0002000)
#define TEST_GRAPH_FRAME UINT32_C(73)
#define TEST_ARENA_WORDS UINT32_C(12)
#define TEST_TERMINATOR_WORD_INDEX UINT32_C(10)

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

static void make_complete_arena(uint32_t* arena) {
    uint32_t i;

    memset(arena, 0, sizeof(uint32_t) * TEST_ARENA_WORDS);
    for (i = 0; i < TEST_TERMINATOR_WORD_INDEX; i += 2) {
        arena[i] = TEST_G_NOOP_W0;
    }
    arena[TEST_TERMINATOR_WORD_INDEX] = TEST_G_ENDDL_W0;
}

static int test_bounded_target_classifications(void) {
    uint32_t arena[TEST_ARENA_WORDS];
    uint32_t terminator_word_index;
    TargetCaptureProbe probe = { 0 };

    make_complete_arena(arena);

    CHECK(graph_classify_task_submission_target(
              arena,
              TEST_ARENA_WORDS,
              &terminator_word_index
          ) == ACGC_GRAPH_SUBMISSION_CAPTURE_COMPLETE);
    CHECK(terminator_word_index == TEST_TERMINATOR_WORD_INDEX);

    graph_set_task_submission_target_capture_callback(
        target_capture_callback,
        &probe
    );
    graph_capture_task_submission_target(
        TEST_TARGET_ID,
        arena,
        TEST_ARENA_WORDS,
        TEST_GRAPH_FRAME
    );
    CHECK(probe.calls == 1);
    CHECK(probe.last_capture.version ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_TARGET_VERSION);
    CHECK(probe.last_capture.graph_frame == TEST_GRAPH_FRAME);
    CHECK(probe.last_capture.target_identity == TEST_TARGET_ID);
    CHECK(probe.last_capture.target_word_capacity == TEST_ARENA_WORDS);
    CHECK(probe.last_capture.captured_word_count ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS);
    CHECK(probe.last_capture.classification ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_PREFIX_ONLY);
    CHECK(probe.last_capture.terminator_word_index ==
          TEST_TERMINATOR_WORD_INDEX);
    CHECK(probe.last_capture.words[0] == TEST_G_NOOP_W0);
    CHECK(probe.last_capture.words[7] == 0);
    graph_clear_task_submission_target_capture_callback();

    CHECK(graph_classify_task_submission_target(
              arena,
              TEST_TERMINATOR_WORD_INDEX,
              &terminator_word_index
          ) == ACGC_GRAPH_SUBMISSION_CAPTURE_UNTERMINATED);
    CHECK(terminator_word_index ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_NO_TERMINATOR);

    arena[TEST_TERMINATOR_WORD_INDEX + 1] = 1;
    CHECK(graph_classify_task_submission_target(
              arena,
              TEST_ARENA_WORDS,
              &terminator_word_index
          ) == ACGC_GRAPH_SUBMISSION_CAPTURE_MALFORMED);
    CHECK(terminator_word_index ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_NO_TERMINATOR);

    return 0;
}

int main(void) {
    CHECK(test_bounded_target_classifications() == 0);
    puts("pc_graph_terminator_fixture: PASS (COMPLETE, PREFIX_ONLY, "
         "UNTERMINATED, and MALFORMED are bounded and fail-closed)");
    return 0;
}
