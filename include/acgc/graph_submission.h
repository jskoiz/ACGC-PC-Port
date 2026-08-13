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
 * fixed-width storage below. The words are copied as uint32_t values only;
 * LP64 static-reference payloads are redacted rather than copied. The words
 * are valid only as a raw submission prefix, not as renderer-specific
 * commands or proof of a visible frame.
 *
 * The source classifier has a separate 256-word bound matching the PC
 * sys_dynamic.work arena. That bound is a readable extent, not a promise that
 * the graph is one contiguous, terminated list: the root may branch into a
 * different game-owned arena. A larger flat snapshot would therefore capture
 * more of the root arena, not the complete graph, and on LP64 it could also
 * expose adjacent native-pointer payload entries.
 */
#define ACGC_GRAPH_SUBMISSION_CAPTURE_VERSION UINT32_C(2)
#define ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS UINT32_C(8)
#define ACGC_GRAPH_SUBMISSION_CAPTURE_SOURCE_MAX_WORDS UINT32_C(256)
#define ACGC_GRAPH_SUBMISSION_CAPTURE_NO_TERMINATOR UINT32_MAX
#define ACGC_GRAPH_SUBMISSION_CAPTURE_REDACTED_WORD UINT32_C(0x50545200)

typedef enum GraphTaskSubmissionCaptureClassification {
    /* A direct, contiguous list ended with an exact G_ENDDL command. */
    ACGC_GRAPH_SUBMISSION_CAPTURE_COMPLETE = 0,
    /* No source words were supplied. */
    ACGC_GRAPH_SUBMISSION_CAPTURE_EMPTY = 1,
    /* The source pointer/capacity pair is invalid. */
    ACGC_GRAPH_SUBMISSION_CAPTURE_INVALID_ARGUMENT = 2,
    /* The source extent exceeds the bounded classifier contract. */
    ACGC_GRAPH_SUBMISSION_CAPTURE_OVERSIZED = 3,
    /* The source is not a sequence of complete two-word Gfx entries. */
    ACGC_GRAPH_SUBMISSION_CAPTURE_MALFORMED = 4,
    /* A direct list has no exact G_ENDDL within the source extent. */
    ACGC_GRAPH_SUBMISSION_CAPTURE_UNTERMINATED = 5,
    /* The source uses a branch/display-list or LP64 static reference. */
    ACGC_GRAPH_SUBMISSION_CAPTURE_INDIRECT = 6,
    /* The source terminates, but the bounded observer prefix does not. */
    ACGC_GRAPH_SUBMISSION_CAPTURE_PREFIX_ONLY = 7
} GraphTaskSubmissionCaptureClassification;

typedef struct GraphTaskSubmissionCapture {
    uint32_t version;
    uint32_t graph_frame;
    uint32_t source_word_capacity;
    uint32_t captured_word_count;
    uint32_t classification;
    uint32_t terminator_word_index;
    uint32_t words[ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS];
} GraphTaskSubmissionCapture;

#if defined(__cplusplus)
static_assert(sizeof(GraphTaskSubmissionCapture) == 56, "graph submission capture must stay fixed-width");
#else
_Static_assert(sizeof(GraphTaskSubmissionCapture) == 56, "graph submission capture must stay fixed-width");
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
 * Classify a bounded source without retaining or following any pointer.
 *
 * COMPLETE requires an exact G_ENDDL pair (w0=0xDF000000, w1=0) before any
 * indirect control-flow command. Zero-filled tail words are never treated as
 * a terminator. On LP64, the adjacent payload of a well-formed static GBI
 * reference must have its fixed trailer and is classified INDIRECT; the
 * native payload is not exposed by this API.
 */
GraphTaskSubmissionCaptureClassification graph_classify_task_submission(
    const void* work_display_list,
    uint32_t source_word_capacity,
    uint32_t* terminator_word_index
);

/*
 * Copy a bounded prefix from the graph-owned work list and notify the
 * optional observer. The observer receives a classification and an exact
 * terminator index when one is present. The caller must provide a valid
 * source_word_capacity when work_display_list is non-NULL; no source pointer
 * or native pointer payload is retained.
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
