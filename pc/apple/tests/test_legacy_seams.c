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

#define TEST_G_ENDDL_W0 UINT32_C(0xDF000000)
#define TEST_G_DL_BRANCH_W0 UINT32_C(0xDE010000)

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
    uint32_t complete_words[ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS] = { 0 };
    uint32_t zero_words[ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS] = { 0 };
    uint32_t long_complete_words[ACGC_GRAPH_SUBMISSION_CAPTURE_SOURCE_MAX_WORDS] = { 0 };
    uint32_t branched_words[ACGC_GRAPH_SUBMISSION_CAPTURE_SOURCE_MAX_WORDS] = { 0 };
    uint32_t oversized_words[ACGC_GRAPH_SUBMISSION_CAPTURE_SOURCE_MAX_WORDS + 1] = { 0 };
#if UINTPTR_MAX > UINT32_MAX
    uint32_t static_reference_words[ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS] = { 0 };
#endif
    void* first_entry = (void*)(uintptr_t)1;
    void* second_entry = (void*)(uintptr_t)2;
    void* entry;
    uint32_t i;

    for (i = 0; i < (uint32_t)(ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS + 2); ++i) {
        game_owned_words[i] = UINT32_C(0xABCD0000) + i;
    }

    graph_set_task_submission_capture_callback(graph_capture_callback, &capture_probe);

    complete_words[0] = UINT32_C(0xE7000000);
    complete_words[1] = 0;
    complete_words[2] = TEST_G_ENDDL_W0;
    complete_words[3] = 0;
    graph_capture_task_submission(
        complete_words,
        ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS,
        UINT32_C(37)
    );
    CHECK(capture_probe.calls == 1);
    CHECK(capture_probe.last_capture.version == ACGC_GRAPH_SUBMISSION_CAPTURE_VERSION);
    CHECK(capture_probe.last_capture.graph_frame == UINT32_C(37));
    CHECK(capture_probe.last_capture.source_word_capacity ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS);
    CHECK(capture_probe.last_capture.captured_word_count ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS);
    CHECK(capture_probe.last_capture.classification ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_COMPLETE);
    CHECK(capture_probe.last_capture.terminator_word_index == 2);
    for (i = 0; i < ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS; ++i) {
        CHECK(capture_probe.last_capture.words[i] == complete_words[i]);
    }

    long_complete_words[0] = UINT32_C(0xE7000000);
    long_complete_words[2] = UINT32_C(0xE7000000);
    long_complete_words[4] = UINT32_C(0xE7000000);
    long_complete_words[6] = UINT32_C(0xE7000000);
    long_complete_words[8] = UINT32_C(0xE7000000);
    long_complete_words[10] = TEST_G_ENDDL_W0;
    graph_capture_task_submission(
        long_complete_words,
        ACGC_GRAPH_SUBMISSION_CAPTURE_SOURCE_MAX_WORDS,
        UINT32_C(38)
    );
    CHECK(capture_probe.calls == 2);
    CHECK(capture_probe.last_capture.source_word_capacity ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_SOURCE_MAX_WORDS);
    CHECK(capture_probe.last_capture.captured_word_count ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS);
    CHECK(capture_probe.last_capture.classification ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_PREFIX_ONLY);
    CHECK(capture_probe.last_capture.terminator_word_index == 10);
    CHECK(capture_probe.last_capture.words[0] == long_complete_words[0]);
    CHECK(capture_probe.last_capture.words[7] == long_complete_words[7]);

    branched_words[0] = TEST_G_DL_BRANCH_W0;
    branched_words[1] = UINT32_C(0xF0002000);
    graph_capture_task_submission(
        branched_words,
        ACGC_GRAPH_SUBMISSION_CAPTURE_SOURCE_MAX_WORDS,
        UINT32_C(39)
    );
    CHECK(capture_probe.calls == 3);
    CHECK(capture_probe.last_capture.graph_frame == UINT32_C(39));
    CHECK(capture_probe.last_capture.source_word_capacity ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_SOURCE_MAX_WORDS);
    CHECK(capture_probe.last_capture.captured_word_count ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS);
    CHECK(capture_probe.last_capture.classification ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_INDIRECT);
    CHECK(capture_probe.last_capture.terminator_word_index ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_NO_TERMINATOR);
    CHECK(capture_probe.last_capture.words[0] == TEST_G_DL_BRANCH_W0);
    CHECK(capture_probe.last_capture.words[1] == UINT32_C(0xF0002000));
    for (i = 2; i < ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS; ++i) {
        CHECK(capture_probe.last_capture.words[i] == 0);
    }

    game_owned_words[0] = UINT32_C(0xABCD0000);
    graph_capture_task_submission(
        game_owned_words,
        (uint32_t)(ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS + 2),
        UINT32_C(40)
    );
    CHECK(capture_probe.calls == 4);
    CHECK(capture_probe.last_capture.classification ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_UNTERMINATED);
    CHECK(capture_probe.last_capture.terminator_word_index ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_NO_TERMINATOR);
    CHECK(capture_probe.last_capture.words[0] == UINT32_C(0xABCD0000));

    graph_capture_task_submission(
        oversized_words,
        ACGC_GRAPH_SUBMISSION_CAPTURE_SOURCE_MAX_WORDS + 1,
        UINT32_C(41)
    );
    CHECK(capture_probe.calls == 5);
    CHECK(capture_probe.last_capture.classification ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_OVERSIZED);

    CHECK(
        graph_classify_task_submission(
            game_owned_words,
            1,
            NULL
        ) == ACGC_GRAPH_SUBMISSION_CAPTURE_MALFORMED
    );
    game_owned_words[0] = TEST_G_ENDDL_W0;
    game_owned_words[1] = UINT32_C(1);
    CHECK(
        graph_classify_task_submission(
            game_owned_words,
            2,
            NULL
        ) == ACGC_GRAPH_SUBMISSION_CAPTURE_MALFORMED
    );

#if UINTPTR_MAX > UINT32_MAX
    static_reference_words[0] = TEST_G_DL_BRANCH_W0;
    static_reference_words[1] = UINT32_C(0xE6F00000);
    static_reference_words[2] = UINT32_C(0x11223344);
    static_reference_words[3] = UINT32_C(0x55667788);
    static_reference_words[4] = UINT32_C(0xA6C0F17E);
    static_reference_words[5] = UINT32_C(0x53A9D421);
    static_reference_words[6] = TEST_G_ENDDL_W0;
    static_reference_words[7] = 0;
    graph_capture_task_submission(
        static_reference_words,
        ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS,
        UINT32_C(42)
    );
    CHECK(capture_probe.calls == 6);
    CHECK(capture_probe.last_capture.classification ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_INDIRECT);
    CHECK(capture_probe.last_capture.words[0] == static_reference_words[0]);
    CHECK(capture_probe.last_capture.words[1] == static_reference_words[1]);
    CHECK(capture_probe.last_capture.words[2] ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_REDACTED_WORD);
    CHECK(capture_probe.last_capture.words[3] ==
          ACGC_GRAPH_SUBMISSION_CAPTURE_REDACTED_WORD);
    CHECK(capture_probe.last_capture.words[4] == static_reference_words[4]);
    CHECK(capture_probe.last_capture.words[5] == static_reference_words[5]);
#endif

    CHECK(
        graph_classify_task_submission(
            complete_words,
            ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS,
            NULL
        ) == ACGC_GRAPH_SUBMISSION_CAPTURE_COMPLETE
    );
    CHECK(
        graph_classify_task_submission(
            zero_words,
            ACGC_GRAPH_SUBMISSION_CAPTURE_MAX_WORDS,
            NULL
        ) == ACGC_GRAPH_SUBMISSION_CAPTURE_UNTERMINATED
    );
    CHECK(
        graph_classify_task_submission(
            branched_words,
            ACGC_GRAPH_SUBMISSION_CAPTURE_SOURCE_MAX_WORDS,
            NULL
        ) == ACGC_GRAPH_SUBMISSION_CAPTURE_INDIRECT
    );
    CHECK(
        graph_classify_task_submission(
            NULL,
            1,
            NULL
        ) == ACGC_GRAPH_SUBMISSION_CAPTURE_INVALID_ARGUMENT
    );
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

    puts("legacy seam tests: PASS (fixed-width graph classification/capture, renderer fallback/override routing, and one-step hot-start semantics)");
    return 0;
}
