#include "acgc/graph_submission.h"

#include <string.h>

static GraphTaskSubmissionCallback graph_task_submission_callback;
static void* graph_task_submission_context;
static GraphTaskSubmissionCaptureCallback graph_task_submission_capture_callback;
static void* graph_task_submission_capture_context;

void graph_set_task_submission_callback(
    GraphTaskSubmissionCallback callback,
    void* context
) {
    graph_task_submission_callback = callback;
    graph_task_submission_context = context;
}

void graph_clear_task_submission_callback(void) {
    graph_task_submission_callback = NULL;
    graph_task_submission_context = NULL;
}

void graph_set_task_submission_capture_callback(
    GraphTaskSubmissionCaptureCallback callback,
    void* context
) {
    graph_task_submission_capture_callback = callback;
    graph_task_submission_capture_context = context;
}

void graph_clear_task_submission_capture_callback(void) {
    graph_task_submission_capture_callback = NULL;
    graph_task_submission_capture_context = NULL;
}

void graph_capture_task_submission(
    const void* work_display_list,
    uint32_t source_word_capacity,
    uint32_t graph_frame
) {
    GraphTaskSubmissionCapture capture = {0};
    uint32_t captured_word_count;

    if (graph_task_submission_capture_callback == NULL) {
        return;
    }

    captured_word_count = source_word_capacity;
    if (captured_word_count > ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS) {
        captured_word_count = ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS;
    }

    capture.version = ACGC_GRAPH_SUBMISSION_CAPTURE_VERSION;
    capture.graph_frame = graph_frame;
    capture.source_word_capacity = source_word_capacity;
    capture.captured_word_count = captured_word_count;
    if (work_display_list == NULL) {
        capture.captured_word_count = 0;
    } else if (captured_word_count != 0) {
        memcpy(
            capture.words,
            work_display_list,
            (size_t)captured_word_count * sizeof(capture.words[0])
        );
    }

    graph_task_submission_capture_callback(
        graph_task_submission_capture_context,
        &capture
    );
}

void graph_submit_task(
    const void* work_display_list,
    GraphTaskSubmissionCallback legacy_fallback,
    void* legacy_context
) {
    if (graph_task_submission_callback != NULL) {
        graph_task_submission_callback(
            graph_task_submission_context,
            work_display_list
        );
    } else if (legacy_fallback != NULL) {
        legacy_fallback(legacy_context, work_display_list);
    }
}
