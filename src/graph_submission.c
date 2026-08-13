#include "acgc/graph_submission.h"

#include <string.h>

#define ACGC_GRAPH_SUBMISSION_G_DL_OPCODE UINT32_C(0xDE)
#define ACGC_GRAPH_SUBMISSION_G_BRANCH_Z_OPCODE UINT32_C(0x04)
#define ACGC_GRAPH_SUBMISSION_G_ENDDL_W0 UINT32_C(0xDF000000)
#define ACGC_GRAPH_SUBMISSION_STATIC_PREFIX UINT32_C(0xE0000000)
#define ACGC_GRAPH_SUBMISSION_STATIC_PREFIX_MASK UINT32_C(0xF0000000)
#define ACGC_GRAPH_SUBMISSION_STATIC_RESERVED_MASK UINT32_C(0x0007FFFF)
#define ACGC_GRAPH_SUBMISSION_STATIC_TRAILER_W0 UINT32_C(0xA6C0F17E)
#define ACGC_GRAPH_SUBMISSION_STATIC_TRAILER_W1 UINT32_C(0x53A9D421)

static GraphTaskSubmissionCallback graph_task_submission_callback;
static void* graph_task_submission_context;
static GraphTaskSubmissionCaptureCallback graph_task_submission_capture_callback;
static void* graph_task_submission_capture_context;

static uint32_t graph_submission_load_word(
    const void* source,
    uint32_t word_index
) {
    uint32_t word;

    memcpy(
        &word,
        (const unsigned char*)source +
            (size_t)word_index * sizeof(word),
        sizeof(word)
    );
    return word;
}

#if UINTPTR_MAX > UINT32_MAX
static int graph_submission_has_static_prefix(uint32_t word) {
    return (word & ACGC_GRAPH_SUBMISSION_STATIC_PREFIX_MASK) ==
           ACGC_GRAPH_SUBMISSION_STATIC_PREFIX;
}

static int graph_submission_has_well_formed_static_tag(uint32_t word) {
    return graph_submission_has_static_prefix(word) &&
           (word & ACGC_GRAPH_SUBMISSION_STATIC_RESERVED_MASK) == 0;
}
#endif

static int graph_submission_is_exact_end(uint32_t w0, uint32_t w1) {
    return w0 == ACGC_GRAPH_SUBMISSION_G_ENDDL_W0 && w1 == 0;
}

static void graph_submission_copy_prefix_pointer_free(
    GraphTaskSubmissionCapture* capture,
    const void* source,
    uint32_t word_count
) {
    uint32_t i = 0;

    while (i < word_count) {
        uint32_t w0 = graph_submission_load_word(source, i);

        capture->words[i++] = w0;
        if (i >= word_count) {
            break;
        }

        {
            uint32_t w1 = graph_submission_load_word(source, i);

            capture->words[i++] = w1;
#if UINTPTR_MAX > UINT32_MAX
            if (graph_submission_has_static_prefix(w1)) {
                uint32_t payload_words = 0;
                uint32_t trailer_words = 0;

                while (payload_words < 2 && i < word_count) {
                    capture->words[i++] = ACGC_GRAPH_SUBMISSION_CAPTURE_REDACTED_WORD;
                    payload_words++;
                }
                while (trailer_words < 2 && i < word_count) {
                    capture->words[i] = graph_submission_load_word(source, i);
                    i++;
                    trailer_words++;
                }
            }
#endif
        }
    }
}

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

GraphTaskSubmissionCaptureClassification graph_classify_task_submission(
    const void* work_display_list,
    uint32_t source_word_capacity,
    uint32_t* terminator_word_index
) {
    uint32_t i;

    if (terminator_word_index != NULL) {
        *terminator_word_index = ACGC_GRAPH_SUBMISSION_CAPTURE_NO_TERMINATOR;
    }

    if (source_word_capacity == 0) {
        return ACGC_GRAPH_SUBMISSION_CAPTURE_EMPTY;
    }
    if (work_display_list == NULL) {
        return ACGC_GRAPH_SUBMISSION_CAPTURE_INVALID_ARGUMENT;
    }
    if (source_word_capacity > ACGC_GRAPH_SUBMISSION_CAPTURE_SOURCE_MAX_WORDS) {
        return ACGC_GRAPH_SUBMISSION_CAPTURE_OVERSIZED;
    }
    if ((source_word_capacity & 1u) != 0) {
        return ACGC_GRAPH_SUBMISSION_CAPTURE_MALFORMED;
    }

    for (i = 0; i < source_word_capacity; i += 2) {
        uint32_t w0 = graph_submission_load_word(work_display_list, i);
        uint32_t w1 = graph_submission_load_word(work_display_list, i + 1);
        uint32_t opcode = w0 >> 24;

#if UINTPTR_MAX > UINT32_MAX
        if (graph_submission_has_static_prefix(w1)) {
            if (!graph_submission_has_well_formed_static_tag(w1) ||
                source_word_capacity - i < 6 ||
                graph_submission_load_word(work_display_list, i + 4) !=
                    ACGC_GRAPH_SUBMISSION_STATIC_TRAILER_W0 ||
                graph_submission_load_word(work_display_list, i + 5) !=
                    ACGC_GRAPH_SUBMISSION_STATIC_TRAILER_W1) {
                return ACGC_GRAPH_SUBMISSION_CAPTURE_MALFORMED;
            }
            return ACGC_GRAPH_SUBMISSION_CAPTURE_INDIRECT;
        }
#endif

        if (opcode == (ACGC_GRAPH_SUBMISSION_G_ENDDL_W0 >> 24)) {
            if (!graph_submission_is_exact_end(w0, w1)) {
                return ACGC_GRAPH_SUBMISSION_CAPTURE_MALFORMED;
            }
            if (terminator_word_index != NULL) {
                *terminator_word_index = i;
            }
            return ACGC_GRAPH_SUBMISSION_CAPTURE_COMPLETE;
        }

        /* G_DL and G_BRANCH_Z transfer control to pointer-bearing state. */
        if (opcode == ACGC_GRAPH_SUBMISSION_G_DL_OPCODE ||
            opcode == ACGC_GRAPH_SUBMISSION_G_BRANCH_Z_OPCODE) {
            return ACGC_GRAPH_SUBMISSION_CAPTURE_INDIRECT;
        }
    }

    return ACGC_GRAPH_SUBMISSION_CAPTURE_UNTERMINATED;
}

void graph_capture_task_submission(
    const void* work_display_list,
    uint32_t source_word_capacity,
    uint32_t graph_frame
) {
    GraphTaskSubmissionCapture capture = {0};
    uint32_t captured_word_count;
    uint32_t terminator_word_index;

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
    capture.terminator_word_index = ACGC_GRAPH_SUBMISSION_CAPTURE_NO_TERMINATOR;
    capture.classification = (uint32_t)graph_classify_task_submission(
        work_display_list,
        source_word_capacity,
        &terminator_word_index
    );
    capture.terminator_word_index = terminator_word_index;
    if (work_display_list == NULL) {
        capture.captured_word_count = 0;
    } else if (captured_word_count != 0) {
        graph_submission_copy_prefix_pointer_free(
            &capture,
            work_display_list,
            captured_word_count
        );
    }

    if (capture.classification == ACGC_GRAPH_SUBMISSION_CAPTURE_COMPLETE &&
        (capture.captured_word_count < 2 ||
         terminator_word_index > capture.captured_word_count - 2)) {
        capture.classification = ACGC_GRAPH_SUBMISSION_CAPTURE_PREFIX_ONLY;
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
