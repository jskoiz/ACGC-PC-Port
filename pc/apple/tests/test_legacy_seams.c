#include "acgc/boot_hot_start.h"
#include "acgc/graph_submission.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct SubmissionProbe {
    int fallback_calls;
    int callback_calls;
    const void* last_payload;
} SubmissionProbe;

typedef struct GraphCaptureProbe {
    int calls;
    GraphTaskSubmissionCapture last_capture;
} GraphCaptureProbe;

static void legacy_fallback(
    void* context,
    const void* work_display_list
) {
    SubmissionProbe* probe = (SubmissionProbe*)context;

    probe->fallback_calls++;
    probe->last_payload = work_display_list;
}

static void platform_callback(
    void* context,
    const void* work_display_list
) {
    SubmissionProbe* probe = (SubmissionProbe*)context;

    probe->callback_calls++;
    probe->last_payload = work_display_list;
}

static void graph_capture_callback(
    void* context,
    const GraphTaskSubmissionCapture* capture
) {
    GraphCaptureProbe* probe = (GraphCaptureProbe*)context;

    probe->calls++;
    probe->last_capture = *capture;
}

typedef struct HotStartProbe {
    int calls;
    void* next_entry;
} HotStartProbe;

static void* advance_hot_start(void* entry, void* context) {
    HotStartProbe* probe = (HotStartProbe*)context;

    (void)entry;
    probe->calls++;
    if (probe->calls == 1) {
        return probe->next_entry;
    }
    return NULL;
}

int main(void) {
    SubmissionProbe submission_probe = { 0 };
    GraphCaptureProbe capture_probe = { 0 };
    HotStartProbe hot_start_probe = { 0 };
    uint32_t game_owned_words[ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS + 2];
    void* first_entry = (void*)(uintptr_t)1;
    void* second_entry = (void*)(uintptr_t)2;
    void* entry;
    uint32_t i;

    for (i = 0; i < (uint32_t)(ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS + 2); ++i) {
        game_owned_words[i] = UINT32_C(0xABCD0000) + i;
    }

    graph_set_task_submission_capture_callback(graph_capture_callback, &capture_probe);
    graph_capture_task_submission(
        game_owned_words,
        (uint32_t)(ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS + 2),
        UINT32_C(37)
    );
    CHECK(capture_probe.calls == 1);
    CHECK(capture_probe.last_capture.version == ACGC_GRAPH_SUBMISSION_CAPTURE_VERSION);
    CHECK(capture_probe.last_capture.graph_frame == UINT32_C(37));
    CHECK(capture_probe.last_capture.source_word_capacity ==
          (uint32_t)(ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS + 2));
    CHECK(capture_probe.last_capture.captured_word_count ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS);
    for (i = 0; i < ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS; ++i) {
        CHECK(capture_probe.last_capture.words[i] == UINT32_C(0xABCD0000) + i);
    }
    game_owned_words[0] = 0;
    CHECK(capture_probe.last_capture.words[0] == UINT32_C(0xABCD0000));
    graph_clear_task_submission_capture_callback();

    graph_clear_task_submission_callback();
    graph_submit_task(first_entry, legacy_fallback, &submission_probe);
    CHECK(submission_probe.fallback_calls == 1);
    CHECK(submission_probe.callback_calls == 0);
    CHECK(submission_probe.last_payload == first_entry);

    graph_set_task_submission_callback(platform_callback, &submission_probe);
    graph_submit_task(second_entry, legacy_fallback, &submission_probe);
    CHECK(submission_probe.fallback_calls == 1);
    CHECK(submission_probe.callback_calls == 1);
    CHECK(submission_probe.last_payload == second_entry);

    graph_clear_task_submission_callback();
    graph_submit_task(second_entry, legacy_fallback, &submission_probe);
    CHECK(submission_probe.fallback_calls == 2);

    hot_start_probe.next_entry = second_entry;
    entry = first_entry;
    CHECK(acgc_boot_hot_start_step(
              &entry,
              advance_hot_start,
              &hot_start_probe
          ) == 1);
    CHECK(entry == second_entry);
    CHECK(hot_start_probe.calls == 1);
    CHECK(acgc_boot_hot_start_step(
              &entry,
              advance_hot_start,
              &hot_start_probe
          ) == 0);
    CHECK(entry == NULL);
    CHECK(hot_start_probe.calls == 2);
    CHECK(acgc_boot_hot_start_step(NULL, advance_hot_start, &hot_start_probe) == 0);

    puts("legacy seam tests: PASS (fixed-width graph prefix capture, renderer fallback/override routing, and one-step hot-start semantics)");
    return 0;
}
