#include "acgc/graph_submission.h"

static GraphTaskSubmissionCallback graph_task_submission_callback;
static void* graph_task_submission_context;

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
