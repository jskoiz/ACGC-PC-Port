#ifndef ACGC_GRAPH_SUBMISSION_H
#define ACGC_GRAPH_SUBMISSION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The payload is opaque to the submission router and to platform callers. */
typedef void (*GraphTaskSubmissionCallback)(
    void* context,
    const void* work_display_list
);

/* Install or clear the optional platform-owned submission callback. */
void graph_set_task_submission_callback(
    GraphTaskSubmissionCallback callback,
    void* context
);
void graph_clear_task_submission_callback(void);

/* Use the installed callback or the supplied legacy fallback exactly once. */
void graph_submit_task(
    const void* work_display_list,
    GraphTaskSubmissionCallback legacy_fallback,
    void* legacy_context
);

#ifdef __cplusplus
}
#endif

#endif /* ACGC_GRAPH_SUBMISSION_H */
