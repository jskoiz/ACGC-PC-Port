#ifndef ACGC_GRAPH_SUBMISSION_H
#define ACGC_GRAPH_SUBMISSION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The payload is opaque to the submission router and to platform callers. */
typedef void (*GraphTaskSubmissionCallback)(
    void* context,
    const void* work_display_list
);

/*
 * A bounded, pointer-free snapshot of the graph-owned work-list prefix.
 *
 * source_word_capacity describes the caller-owned readable extent; it is not
 * an inferred display-list length. captured_word_count is capped at the
 * fixed-width storage below. The words are copied in their native uint32_t
 * representation and are valid only as a raw submission prefix, not as
 * renderer-specific commands or proof of a visible frame.
 */
#define ACGC_GRAPH_SUBMISSION_CAPTURE_VERSION UINT32_C(1)
#define ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS UINT32_C(8)

typedef struct GraphTaskSubmissionCapture {
    uint32_t version;
    uint32_t graph_frame;
    uint32_t source_word_capacity;
    uint32_t captured_word_count;
    uint32_t words[ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS];
} GraphTaskSubmissionCapture;

#if defined(__cplusplus)
static_assert(sizeof(GraphTaskSubmissionCapture) == 48, "graph submission capture must stay fixed-width");
#else
_Static_assert(sizeof(GraphTaskSubmissionCapture) == 48, "graph submission capture must stay fixed-width");
#endif

typedef void (*GraphTaskSubmissionCaptureCallback)(
    void* context,
    const GraphTaskSubmissionCapture* capture
);

/* Install or clear the optional platform-owned submission callback. */
void graph_set_task_submission_callback(
    GraphTaskSubmissionCallback callback,
    void* context
);
void graph_clear_task_submission_callback(void);

/* Install or clear an optional observer for a bounded graph-owned snapshot. */
void graph_set_task_submission_capture_callback(
    GraphTaskSubmissionCaptureCallback callback,
    void* context
);
void graph_clear_task_submission_capture_callback(void);

/*
 * Copy a bounded prefix from the graph-owned work list and notify the
 * optional observer. The caller must provide a valid source_word_capacity
 * when work_display_list is non-NULL; no source pointer is retained.
 */
void graph_capture_task_submission(
    const void* work_display_list,
    uint32_t source_word_capacity,
    uint32_t graph_frame
);

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
