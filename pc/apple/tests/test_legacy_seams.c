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
    HotStartProbe hot_start_probe = { 0 };
    void* first_entry = (void*)(uintptr_t)1;
    void* second_entry = (void*)(uintptr_t)2;
    void* entry;

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

    puts("legacy seam tests: PASS (renderer fallback/override routing and one-step hot-start semantics)");
    return 0;
}
